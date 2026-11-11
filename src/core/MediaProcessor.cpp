#include "core/MediaProcessor.hpp"
#include "config/PathHelper.hpp"
#include "core/FileUtils.hpp"
#include "core/Logger.hpp"
#include "core/MediaProbe.hpp"
#include "core/ProcessUtils.hpp"
#include "scrapers/ScraperUtils.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <random>
#include <regex>
#include <system_error>
#include <thread>
#include <utility>
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

namespace Core {

    namespace {
        // Stesso filesystem = rename atomico senza copia. Su Windows il
        // confronto delle radici (C:, D:) evita che MoveFileEx faccia una
        // copia cross-volume non atomica.
        bool sameDevice(const fs::path& a, const fs::path& b) {
#ifdef _WIN32
            return a.root_name() == b.root_name();
#else
            struct stat sa{};
            struct stat sb{};
            if (::stat(a.c_str(), &sa) != 0 || ::stat(b.c_str(), &sb) != 0)
                return false;
            return sa.st_dev == sb.st_dev;
#endif
        }
    } // namespace

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::uint64_t estimateConversionBytes(std::uint64_t inputBytes, int chunksPerTask) {
        // 286/100 = 2.2 (lavoro) × 1.3 (margine); 156/100 = 1.2 × 1.3.
        // Aritmetica intera: gli input sono file size, nessun overflow.
        const std::uint64_t num = chunksPerTask > 1 ? 286 : 156;
        return (inputBytes * num + 99) / 100;
    }

    std::optional<fs::path> chooseWorkRoot(const std::vector<fs::path>& ramRoots,
                                           const fs::path& seriesDir, const fs::path& cacheDir,
                                           std::uint64_t neededBytes) {
        auto fits = [neededBytes](const fs::path& dir) {
            std::error_code ec;
            if (dir.empty() || !fs::is_directory(dir, ec) || ec)
                return false;
            const auto info = fs::space(dir, ec);
            return !ec && info.available >= neededBytes;
        };
        for (const auto& root : ramRoots)
            if (fits(root))
                return root;
        if (fits(seriesDir))
            return seriesDir;
        if (fits(cacheDir))
            return cacheDir;
        return std::nullopt;
    }

    std::mutex MediaProcessor::s_convMutex;
    std::condition_variable MediaProcessor::s_convCv;
    std::atomic<int> MediaProcessor::s_activeConversions{0};

    MediaProcessor::MediaProcessor(ProgressCallback callback, std::atomic<bool>& stopSignal,
                                   TaskProgressCallback activity)
        : m_progressCallback(std::move(callback)), m_activity(std::move(activity)),
          m_stopSignal(stopSignal) {}

    void MediaProcessor::activity(TaskPhase phase, int percent, const std::string& detail) {
        if (m_activity)
            m_activity({phase, percent < 0 ? -1 : std::clamp(percent, 0, 100), detail});
    }

    void MediaProcessor::notifyStop() {
        std::lock_guard<std::mutex> lock(s_convMutex);
        s_convCv.notify_all();
    }

    void MediaProcessor::cleanupOrphanWorkDirs(const std::vector<std::string>& seriesPaths) {
        int removed = 0;
        auto cleanRoot = [&removed](const fs::path& root) {
            if (root.empty())
                return;
            std::error_code ec;
            if (!fs::is_directory(root, ec) || ec)
                return;
            for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
                const auto name = it->path().filename().string();
                const bool isProc =
                    name.rfind(".anidown_proc_", 0) == 0 || name.rfind("anidown_proc_", 0) == 0;
                const bool isPublish = name.rfind(".anidown_publish_", 0) == 0;
                if (!isProc && !isPublish)
                    continue;
                std::error_code rmEc;
                const auto n = fs::remove_all(it->path(), rmEc);
                if (!rmEc)
                    removed += static_cast<int>(n);
            }
        };

        for (const auto& p : seriesPaths)
            if (!p.empty())
                cleanRoot(fs::path(ScraperUtils::expandTilde(p)));
        for (const auto& root : Config::PathHelper::getRamWorkDirs())
            cleanRoot(root);
        cleanRoot(Config::PathHelper::getCacheDir() / "work");

        // Legacy pre-fix: workdir creati direttamente nella tmp di sistema.
        std::error_code tmpEc;
        cleanRoot(fs::temp_directory_path(tmpEc));
#ifdef __linux__
        cleanRoot("/dev/shm");
#endif

        if (removed > 0)
            Logger::info(std::format("Rimossi {} residui temporanei di conversione", removed));
    }

    ProcessResult MediaProcessor::processTask(const DownloadTask& task, const Series& series,
                                              const Config::ExecutionStrategy& strategy) {
        ProcessResult res;
        res.episodeNumber = task.episodeNumber;
        if (task.fileName.empty()) {
            res.errorMessage = "Nome file mancante";
            Logger::error(series.name + ": task senza fileName, salto");
            return res;
        }
        std::string expandedPath = ScraperUtils::expandTilde(series.path);
        std::string fullFile = (fs::path(expandedPath) / task.fileName).string();
        std::string aria2File = fullFile + ".aria2";
        std::string partFile = fullFile + ".part";
        std::string partAria2 = partFile + ".aria2";

        // La cartella serie può non esistere (serie nuova, path appena scelto,
        // SSD non montato): aria2 --dir su una dir inesistente fallisce.
        std::error_code dirEc;
        fs::create_directories(expandedPath, dirEc);
        if (dirEc) {
            res.errorMessage = "Impossibile creare la cartella della serie: " + dirEc.message();
            Logger::error(series.name + ": " + res.errorMessage + " (" + expandedPath + ")");
            return res;
        }

        bool needsDownload = true;
        bool needsConversion = strategy.convertToH265;

        // --- PRE-CHECK: MACCHINA A STATI (feature 11) ---
        // | finale sano                      | skip / conversione locale       |
        // | finale + .aria2 (legacy pre-11)  | delete entrambi → download .part|
        // | finale corrotto                  | delete → download .part         |
        // | .part + .part.aria2              | resume (--continue) → publish   |
        // | .part senza .aria2 (no proof)    | delete → download da zero       |
        if (fs::exists(fullFile)) {
            activity(TaskPhase::Queued, -1, "Verifica file esistente");
            m_progressCallback(series.name, "Verifica file esistente...");

            if (fs::exists(aria2File)) {
                m_progressCallback(series.name,
                                   "Rilevato download incompleto legacy, rimozione residui...");
                std::error_code ec;
                fs::remove(fullFile, ec);
                fs::remove(aria2File, ec);
            } else if (!MediaProbe::isMediaFileHealthy(fullFile, m_stopSignal)) {
                m_progressCallback(series.name, "Rilevato file corrotto, rimozione in corso...");
                std::error_code ec;
                fs::remove(fullFile, ec);
            } else {
                std::string codec = MediaProbe::getVideoCodec(fullFile, m_stopSignal);

                if (strategy.convertToH265) {
                    if (codec == "hevc" || codec == "h265") {
                        m_progressCallback(series.name, "✅ Già completato (H265)");
                        res.success = true;
                        res.filePath = fullFile;
                        return res;
                    } else {
                        m_progressCallback(series.name, "Avvio conversione su file esistente...");
                        needsDownload = false;
                    }
                } else {
                    m_progressCallback(series.name, "✅ Già completato");
                    res.success = true;
                    res.filePath = fullFile;
                    return res;
                }
            }
        }

        // Residuo .part da un run precedente (crash o stop):
        if (fs::exists(partFile) || fs::exists(partAria2)) {
            if (fs::exists(partAria2)) {
                // Download interrotto: il resume lo riprende dal punto di
                // interruzione (--continue). Se la sorgente è cambiata, aria2
                // riparte da zero da solo.
                m_progressCallback(series.name, "Ripresa download interrotto...");
                needsDownload = true;
            } else {
                // Nessun controllo di controllo aria2: nessuna prova che il
                // .part corrisponda alla sorgente → delete + redownload.
                m_progressCallback(series.name, "Residuo .part senza prova di integrità, "
                                                "rimozione...");
                std::error_code ec;
                fs::remove(partFile, ec);
                fs::remove(partAria2, ec);
                needsDownload = true;
            }
        }

        // Task di conversione locale (videoUrl vuoto) con file sorgente mancante:
        // fallisci subito invece di 3 retry di aria2 con URL vuoto.
        if (needsDownload && task.videoUrl.empty()) {
            res.errorMessage = "File sorgente mancante per la conversione locale";
            Logger::error(
                std::format("{}: file sorgente mancante per la conversione locale (Ep. {})",
                            series.name, task.episodeNumber));
            return res;
        }

        if (needsDownload && needsConversion)
            m_progressCallback(series.name, "MODE:BOTH");
        else if (needsDownload)
            m_progressCallback(series.name, "MODE:DL");
        else
            m_progressCallback(series.name, "MODE:CONV");

        // --- FASE 1: DOWNLOAD CON ARIA2C (in .part, con resume) ---
        auto startDl = std::chrono::steady_clock::now();
        if (needsDownload) {
            activity(TaskPhase::Downloading, 0, "Trasferimento in corso");
            m_progressCallback(series.name, "Download...");
            // --continue=true: i trasferimenti interrotti riprendono dal punto
            // di interruzione (i .part non vengono MAI cancellati tra i
            // tentativi, altrimenti il resume sarebbe distrutto). Se la
            // sorgente è cambiata, aria2 riparte da zero da solo.
            // --auto-save-interval=1: il control file .aria2 viene salvato ogni
            // secondo → un crash (SIGKILL) perde al massimo 1s di progresso.
            std::string dlCmd =
                "aria2c -x 16 -s 16 --summary-interval=1 --continue=true --auto-save-interval=1 "
                "--allow-overwrite=true "
                "--dir=" +
                ScraperUtils::Q(expandedPath) + " -o " + ScraperUtils::Q(task.fileName + ".part") +
                " " + ScraperUtils::Q(task.videoUrl);

            static const std::regex dlRegex(R"raw(\((\d+)%\))raw");
            int status = 0;
            for (int attempt = 1; attempt <= 3; ++attempt) {
                status =
                    ProcessUtils::runCommand(dlCmd, m_stopSignal, [&](const std::string& line) {
                        std::smatch m;
                        if (std::regex_search(line, m, dlRegex)) {
                            activity(TaskPhase::Downloading, std::stoi(m[1].str()),
                                     "Trasferimento in corso");
                            m_progressCallback(series.name, "DL " + m[1].str() + "%");
                        }
                    });

                if (status == 0 || m_stopSignal || attempt == 3)
                    break;

                Logger::warn(std::format("{}: Download fallito, tentativo {}/3, retry tra 3s...",
                                         series.name, attempt));
                m_progressCallback(series.name, std::format("Retry download ({}/3)...", attempt));
                activity(TaskPhase::Downloading, -1,
                         std::format("Nuovo tentativo download ({}/3)", attempt + 1));
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }

            if (status != 0) {
                res.errorMessage =
                    m_stopSignal ? "Interrotto dall'utente" : "Errore nel download per aria2";
                Logger::error(series.name + ": Download fallito dopo 3 tentativi");
                return res;
            }

            // Check di integrità sul .part PRIMA della pubblicazione: nessun
            // file corrotto può finire in media (la garanzia strutturale S1).
            m_progressCallback(series.name, "Verifica file scaricato...");
            activity(TaskPhase::VerifyingDownload, -1,
                     "Trasferimento concluso · verifica integrità");
            if (!MediaProbe::isMediaFileHealthy(partFile, m_stopSignal)) {
                res.errorMessage = "File scaricato non valido (check di integrità fallito)";
                Logger::error(series.name + ": " + res.errorMessage);
                std::error_code ec;
                fs::remove(partFile, ec);
                fs::remove(partAria2, ec);
                return res;
            }

            // Publish atomico no-replace: il nome finale non deve esistere.
            // In caso di Exists (file finale apparso nel frattempo) il lavoro è
            // già compiuto: il .part è stato validato sano prima, quindi si
            // puliscono i parziali e si conferma. Se il finale in conflitto è
            // corrotto NON lo si cancella: si segnala il path, il .part resta per
            // il run successivo (mai distruggere l'unica copia su disco).
            int publishErrno = 0;
            auto publishStatus = publishNoReplace(partFile, fullFile, &publishErrno);
            if (publishStatus == PublishStatus::Exists) {
                if (MediaProbe::isMediaFileHealthy(fullFile, m_stopSignal)) {
                    std::error_code ec;
                    fs::remove(partFile, ec);
                    fs::remove(partAria2, ec);
                    res.downloadTime =
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - startDl)
                            .count();
                    res.success = true;
                    res.filePath = fullFile;
                    Logger::info(series.name + ": episodio già presente, salto il publish");
                    return res;
                }
                res.errorMessage = "Il file esistente è corrotto: rimuovilo manualmente (" +
                                   fullFile + ") per completare il publish";
                Logger::error(series.name + ": " + res.errorMessage);
                return res;
            }
            if (publishStatus == PublishStatus::NoAtomicSupport) {
                res.errorMessage = "Publish fallito: il filesystem non supporta "
                                   "il rilascio atomico no-replace (nessun hard link)";
                Logger::error(series.name + ": " + res.errorMessage);
                return res;
            }
            if (publishStatus == PublishStatus::Error) {
                res.errorMessage = "Publish fallito: errore filesystem";
                if (publishErrno != 0)
                    res.errorMessage += " (errno " + std::to_string(publishErrno) + ")";
                Logger::error(series.name + ": " + res.errorMessage);
                return res;
            }

            res.downloadTime =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - startDl).count();
        } else {
            res.downloadTime = 0.0;
        }

        // --- FASE 2: CONVERSIONE CON FFmpeg ---
        if (needsConversion) {
            activity(TaskPhase::WaitingConversion, -1, "Download completo · attesa di uno slot");
            m_progressCallback(series.name, "In coda...");
            {
                std::unique_lock<std::mutex> lock(s_convMutex);
                s_convCv.wait(lock, [&] {
                    return s_activeConversions < strategy.maxConcurrentTasks || m_stopSignal;
                });
                if (m_stopSignal) {
                    res.errorMessage = "Interrotto dall'utente";
                    return res;
                }
                s_activeConversions++;
            }

            activity(TaskPhase::Converting, -1, "Preparazione conversione");
            bool ok = convertAndVerify(fullFile, series.name, strategy, res.conversionTime);

            {
                std::lock_guard<std::mutex> lock(s_convMutex);
                s_activeConversions--;
            }
            s_convCv.notify_one();

            if (!ok) {
                res.errorMessage =
                    m_stopSignal ? "Interrotto dall'utente" : "Errore nella compressione del video";
                return res;
            }
        } else {
            res.conversionTime = 0.0;
        }

        res.success = true;
        res.filePath = fullFile;
        m_progressCallback(series.name, "✅ Completato");
        return res;
    }

    bool MediaProcessor::convertFile(const std::string& inputPath, const std::string& seriesName,
                                     const Config::ExecutionStrategy& strategy, double& outTime) {
        return convertAndVerify(inputPath, seriesName, strategy, outTime);
    }

    bool MediaProcessor::convertAndVerify(const std::string& inputPath,
                                          const std::string& seriesName,
                                          const Config::ExecutionStrategy& strategy,
                                          double& outTime) {
        const int MAX_RETRIES = 2;
        auto start = std::chrono::steady_clock::now();
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<unsigned long long> dis(1000, 9999);

        // Feature 11 — fail-fast su cause deterministiche (niente retry inutili):
        // input illeggibile o binari ffmpeg/ffprobe assenti non diventano
        // mai "transienti" tra un tentativo e l'altro.
        if (!fs::exists(inputPath) || fs::file_size(inputPath) == 0) {
            Logger::error(seriesName + ": input illeggibile per la conversione (" + inputPath +
                          ")");
            return false;
        }
        if (!MediaProbe::isToolAvailable("ffmpeg") || !MediaProbe::isToolAvailable("ffprobe")) {
            Logger::error(seriesName + ": ffmpeg/ffprobe non trovati nel PATH, conversione "
                                       "impossibile");
            return false;
        }

        // Workdir: RAM → cartella serie (rename atomico) → cache. La scelta
        // usa solo fs::space (una syscall) e la stima O(1) del fabbisogno.
        const auto inputSize = static_cast<std::uint64_t>(fs::file_size(inputPath));
        const auto needed = estimateConversionBytes(inputSize, strategy.chunksPerTask);
        const fs::path seriesDir = fs::path(inputPath).parent_path();
        const fs::path cacheWork = Config::PathHelper::getCacheDir() / "work";
        {
            std::error_code ec;
            fs::create_directories(cacheWork, ec); // se fallisce, chooseWorkRoot la scarta
        }
        std::vector<fs::path> ramRoots = Config::PathHelper::getRamWorkDirs();
        for (const auto& root : ramRoots) {
            std::error_code ec;
            fs::create_directories(root, ec); // se fallisce, chooseWorkRoot la scarta
        }
        const auto workRoot = chooseWorkRoot(ramRoots, seriesDir, cacheWork, needed);
        if (!workRoot) {
            Logger::error(std::format("{}: spazio insufficiente per la conversione "
                                      "(servono ~{} MiB liberi tra RAM, cartella serie e cache)",
                                      seriesName, needed / (1024ULL * 1024ULL)));
            return false;
        }
        // Se il workdir non è sul device di destinazione, il publish copia il
        // risultato in un temp accanto al finale: quello spazio serve subito.
        const auto dstSpace = diskSpace(seriesDir.string());
        if (dstSpace.freeBytes < inputSize + inputSize / 10) {
            Logger::error(std::format("{}: spazio insufficiente nella destinazione per il "
                                      "salvataggio (~{} MiB liberi richiesti)",
                                      seriesName, inputSize / (1024ULL * 1024ULL)));
            return false;
        }

        for (int attempt = 1; attempt <= MAX_RETRIES; ++attempt) {
            if (m_stopSignal)
                return false;

            m_progressCallback(seriesName, "Analisi...");
            activity(TaskPhase::Converting, -1, "Analisi del video");

            std::string uniqueId = std::to_string(std::hash<std::string>{}(inputPath)) + "_" +
                                   std::to_string(dis(gen));
            fs::path workDir = *workRoot / (".anidown_proc_" + uniqueId);

            std::error_code workEc;
            fs::remove_all(workDir, workEc);
            fs::create_directories(workDir, workEc);
            if (workEc) {
                Logger::error(std::format("{}: impossibile creare il workdir {}: {}", seriesName,
                                          workDir.string(), workEc.message()));
                return false;
            }

            try {
                double duration = MediaProbe::getVideoDuration(inputPath, m_stopSignal);
                if (duration <= 0)
                    throw std::runtime_error("Impossibile leggere durata");

#ifndef _WIN32
                std::string nicePrefix = "nice -n 19 ";
#else
                std::string nicePrefix = "start \"\" /b /low ";
#endif

                // CRF 25: -19% di bitrate rispetto a CRF 23 con VMAF 93.2
                // (94.5 a CRF 23), sotto la soglia di percezione.
                std::string baseArgs =
                    std::format("-c:v libx265 -crf 25 -preset veryfast -threads {} "
                                "-x265-params \"hist-scenecut=1\" -c:a copy",
                                strategy.threadsPerFFmpeg);

                fs::path finalMergedInWork = workDir / "merged.mp4";

                if (strategy.chunksPerTask <= 1) {
                    activity(TaskPhase::Converting, 0, "Codifica HEVC");
                    m_progressCallback(seriesName, "Encoding...");
                    fs::path progP = workDir / "prog.txt";
                    std::string cmd =
                        nicePrefix + "ffmpeg -v error -y -i " + ScraperUtils::Q(inputPath);
                    cmd += " ";
                    cmd += baseArgs;
                    cmd += " -progress ";
                    cmd += ScraperUtils::Q(progP.string());
                    cmd += " ";
                    cmd += ScraperUtils::Q(finalMergedInWork.string());
                    cmd += ScraperUtils::DEVNULL();

                    auto f = std::async(std::launch::async, [&cmd, this]() {
                        return ProcessUtils::runCommand(cmd, m_stopSignal, nullptr);
                    });
                    while (f.wait_for(std::chrono::milliseconds(500)) !=
                           std::future_status::ready) {
                        if (m_stopSignal) {
                            fs::remove_all(workDir);
                            return false;
                        }
                        activity(TaskPhase::Converting,
                                 (int)((ProcessUtils::parseProgressUs(progP) * 100) /
                                       (duration * 1000000)),
                                 "Codifica HEVC");
                        m_progressCallback(
                            seriesName,
                            std::format(
                                "Conv {}%",
                                (std::min)(100, (int)((ProcessUtils::parseProgressUs(progP) * 100) /
                                                      (duration * 1000000)))));
                    }
                    if (f.get() != 0 || m_stopSignal) {
                        fs::remove_all(workDir);
                        return false;
                    }
                } else {
                    activity(TaskPhase::Converting, -1, "Preparazione segmenti");
                    m_progressCallback(seriesName, "Splitting...");
                    std::string split =
                        "ffmpeg -v error -y -i " + ScraperUtils::Q(inputPath) +
                        " -c copy -map 0 -f segment -segment_time " +
                        ProcessUtils::formatFloat(duration / strategy.chunksPerTask) +
                        " -reset_timestamps 1 " + ScraperUtils::Q((workDir / "s%03d.mp4").string());
                    if (ProcessUtils::runCommand(split, m_stopSignal, nullptr) != 0)
                        throw std::runtime_error("Errore split");

                    std::vector<fs::path> parts;
                    for (const auto& p : fs::directory_iterator(workDir))
                        if (p.path().filename().string().find('s') == 0)
                            parts.push_back(p.path());
                    std::sort(parts.begin(), parts.end());

                    activity(TaskPhase::Converting, 0, "Codifica HEVC");
                    m_progressCallback(seriesName, "Encoding...");
                    struct Job {
                        fs::path out;
                        fs::path prog;
                        std::future<int> f;
                    };
                    std::vector<Job> jobs;
                    for (const auto& p : parts) {
                        fs::path outP = p.string() + ".enc.mp4";
                        fs::path progP = p.string() + ".txt";
                        std::string cmd = nicePrefix + "ffmpeg -v error -y -i ";
                        cmd += ScraperUtils::Q(p.string());
                        cmd += " ";
                        cmd += baseArgs;
                        cmd += " -progress ";
                        cmd += ScraperUtils::Q(progP.string());
                        cmd += " ";
                        cmd += ScraperUtils::Q(outP.string());
                        cmd += ScraperUtils::DEVNULL();
                        auto ffJob =
                            std::async(std::launch::async, [cmd, stopSig = &m_stopSignal]() -> int {
                                return ProcessUtils::runCommand(cmd, *stopSig, nullptr);
                            });
                        jobs.push_back({outP, progP, std::move(ffJob)});
                    }

                    while (true) {
                        if (m_stopSignal) {
                            for (auto& j : jobs)
                                j.f.wait();
                            fs::remove_all(workDir);
                            return false;
                        }
                        bool allDone = true;
                        double cur = 0;
                        for (auto& j : jobs) {
                            if (j.f.wait_for(std::chrono::milliseconds(0)) !=
                                std::future_status::ready)
                                allDone = false;
                            cur += ProcessUtils::parseProgressUs(j.prog);
                        }
                        activity(TaskPhase::Converting, (int)((cur * 100) / (duration * 1000000)),
                                 "Codifica HEVC");
                        m_progressCallback(
                            seriesName,
                            std::format("Conv {}%", (std::min)(100, (int)((cur * 100) /
                                                                          (duration * 1000000)))));
                        if (allDone)
                            break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    }
                    for (auto& j : jobs)
                        if (j.f.get() != 0)
                            throw std::runtime_error("Errore in un chunk");

                    m_progressCallback(seriesName, "Merging...");
                    activity(TaskPhase::Finalizing, -1, "Unione dei segmenti");
                    std::ofstream l(workDir / "list.txt");
                    for (auto& j : jobs)
                        l << "file '" << j.out.filename().string() << "'\n";
                    l.close();
                    std::string concat =
                        "ffmpeg -v error -y -fflags +genpts -f concat -safe 0 -i " +
                        ScraperUtils::Q((workDir / "list.txt").string()) + " -c copy " +
                        ScraperUtils::Q(finalMergedInWork.string()) + ScraperUtils::DEVNULL();
                    if (ProcessUtils::runCommand(concat, m_stopSignal, nullptr) != 0)
                        throw std::runtime_error("Errore merging");
                }

                m_progressCallback(seriesName, "Verifica...");
                activity(TaskPhase::Finalizing, -1, "Verifica del video convertito");
                if (MediaProbe::verifyIntegrity(finalMergedInWork.string(), duration,
                                                m_stopSignal)) {

                    m_progressCallback(seriesName, "Salvataggio...");
                    activity(TaskPhase::Finalizing, -1, "Salvataggio del video");

                    std::error_code ec;
                    int publishErrno = 0;
                    bool published = false;
                    if (sameDevice(finalMergedInWork, inputPath)) {
                        published = publishReplace(finalMergedInWork.string(), inputPath,
                                                   &publishErrno) == PublishStatus::Success;
                    }
                    if (!published) {
                        // Workdir su un altro device (RAM/cache): copia in un
                        // temp accanto al finale, poi rename atomico. Il file
                        // vecchio resta intatto fino al rename: un crash non
                        // può lasciare un finale troncato.
                        const fs::path publishTmp = fs::path(inputPath).parent_path() /
                                                    (".anidown_publish_" + uniqueId + ".tmp");
                        fs::remove(publishTmp, ec);
                        fs::copy_file(finalMergedInWork, publishTmp,
                                      fs::copy_options::overwrite_existing, ec);
                        if (ec) {
                            std::error_code rmEc;
                            fs::remove(publishTmp, rmEc);
                            throw std::runtime_error("Impossibile copiare file sul disco finale: " +
                                                     ec.message());
                        }
                        if (publishReplace(publishTmp.string(), inputPath, &publishErrno) !=
                            PublishStatus::Success) {
                            std::error_code rmEc;
                            fs::remove(publishTmp, rmEc);
                            throw std::runtime_error("Publish atomico fallito (errno " +
                                                     std::to_string(publishErrno) + ")");
                        }
                    }

                    fs::remove_all(workDir);
                    outTime =
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
                            .count();
                    return true;
                } else
                    throw std::runtime_error("Verifica file unito fallita");

            } catch (const std::exception& e) {
                Logger::error(
                    std::format("{}: Tentativo {} fallito: {}", seriesName, attempt, e.what()));
                fs::remove_all(workDir);
                if (attempt == MAX_RETRIES)
                    break;
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
        return false;
    }

} // namespace Core
