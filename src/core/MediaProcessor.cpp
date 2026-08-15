#include "core/MediaProcessor.hpp"
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

namespace fs = std::filesystem;

namespace Core {

    std::mutex MediaProcessor::s_convMutex;
    std::condition_variable MediaProcessor::s_convCv;
    std::atomic<int> MediaProcessor::s_activeConversions{0};

    MediaProcessor::MediaProcessor(ProgressCallback callback, std::atomic<bool>& stopSignal)
        : m_progressCallback(callback), m_stopSignal(stopSignal) {}

    void MediaProcessor::notifyStop() {
        std::lock_guard<std::mutex> lock(s_convMutex);
        s_convCv.notify_all();
    }

    ProcessResult MediaProcessor::processTask(const DownloadTask& task, const Series& series,
                                              const Config::ExecutionStrategy& strategy) {
        ProcessResult res;
        res.episodeNumber = task.episodeNumber;
        std::string expandedPath = ScraperUtils::expandTilde(series.path);
        std::string fullFile = (fs::path(expandedPath) / task.fileName).string();
        std::string aria2File = fullFile + ".aria2";
        std::string partFile = fullFile + ".part";
        std::string partAria2 = partFile + ".aria2";

        bool needsDownload = true;
        bool needsConversion = strategy.convertToH265;

        // --- PRE-CHECK: MACCHINA A STATI (feature 11) ---
        // | finale sano                      | skip / conversione locale       |
        // | finale + .aria2 (legacy pre-11)  | delete entrambi → download .part|
        // | finale corrotto                  | delete → download .part         |
        // | .part + .part.aria2              | resume (--continue) → publish   |
        // | .part senza .aria2 (no proof)    | delete → download da zero       |
        if (fs::exists(fullFile)) {
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
                        return res;
                    } else {
                        m_progressCallback(series.name, "Avvio conversione su file esistente...");
                        needsDownload = false;
                    }
                } else {
                    m_progressCallback(series.name, "✅ Già completato");
                    res.success = true;
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
            m_progressCallback(series.name, "Download...");
            // --continue=true: i trasferimenti interrotti riprendono dal punto
            // di interruzione (i .part non vengono MAI cancellati tra i
            // tentativi, altrimenti il resume sarebbe distrutto). Se la
            // sorgente è cambiata, aria2 riparte da zero da solo.
            std::string dlCmd =
                "aria2c -x 16 -s 16 --summary-interval=1 --continue=true --allow-overwrite=true "
                "--dir=" +
                ScraperUtils::Q(expandedPath) + " -o " + ScraperUtils::Q(task.fileName + ".part") +
                " " + ScraperUtils::Q(task.videoUrl);

            static const std::regex dlRegex(R"raw(\((\d+)%\))raw");
            int status = 0;
            for (int attempt = 1; attempt <= 3; ++attempt) {
                status =
                    ProcessUtils::runCommand(dlCmd, m_stopSignal, [&](const std::string& line) {
                        std::smatch m;
                        if (std::regex_search(line, m, dlRegex))
                            m_progressCallback(series.name, "DL " + m[1].str() + "%");
                    });

                if (status == 0 || m_stopSignal || attempt == 3)
                    break;

                Logger::warn(std::format("{}: Download fallito, tentativo {}/3, retry tra 3s...",
                                         series.name, attempt));
                m_progressCallback(series.name, std::format("Retry download ({}/3)...", attempt));
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
            if (!MediaProbe::isMediaFileHealthy(partFile, m_stopSignal)) {
                res.errorMessage = "File scaricato non valido (check di integrità fallito)";
                Logger::error(series.name + ": " + res.errorMessage);
                std::error_code ec;
                fs::remove(partFile, ec);
                fs::remove(partAria2, ec);
                return res;
            }

            // Publish atomico no-replace: il nome finale non deve esistere.
            // In caso di EEXIST (o primitiva non disponibile) il task fallisce
            // e il .part resta per il run successivo (limite noto del piano).
            if (!publishNoReplace(partFile, fullFile)) {
                res.errorMessage = "Publish fallito: il file finale esiste già";
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
        m_progressCallback(series.name, "✅ Completato");
        return res;
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

        for (int attempt = 1; attempt <= MAX_RETRIES; ++attempt) {
            if (m_stopSignal)
                return false;

            m_progressCallback(seriesName, "Analisi...");

            bool isRamDisk = (ProcessUtils::getRamUsagePercent() < 55.0 && fs::exists("/dev/shm"));
            fs::path baseWorkDir = isRamDisk ? fs::path("/dev/shm") : fs::temp_directory_path();

            std::string uniqueId = std::to_string(std::hash<std::string>{}(inputPath)) + "_" +
                                   std::to_string(dis(gen));
            fs::path workDir = baseWorkDir / ("anidown_proc_" + uniqueId);

            fs::remove_all(workDir);
            fs::create_directories(workDir);

            try {
                double duration = MediaProbe::getVideoDuration(inputPath, m_stopSignal);
                if (duration <= 0)
                    throw std::runtime_error("Impossibile leggere durata");

#ifndef _WIN32
                std::string nicePrefix = "nice -n 19 ";
#else
                std::string nicePrefix = "start \"\" /b /low ";
#endif

                std::string baseArgs =
                    std::format("-c:v libx265 -crf 23 -preset veryfast -threads {} "
                                "-x265-params \"hist-scenecut=1\" -c:a copy",
                                strategy.threadsPerFFmpeg);

                fs::path finalMergedInWork = workDir / "merged.mp4";

                if (strategy.chunksPerTask <= 1) {
                    m_progressCallback(seriesName, "Encoding...");
                    fs::path progP = workDir / "prog.txt";
                    std::string cmd =
                        nicePrefix + "ffmpeg -v error -y -i " + ScraperUtils::Q(inputPath) + " " +
                        baseArgs + " -progress " + ScraperUtils::Q(progP.string()) + " " +
                        ScraperUtils::Q(finalMergedInWork.string()) + ScraperUtils::DEVNULL();

                    auto f = std::async(std::launch::async, [&cmd, this]() {
                        return ProcessUtils::runCommand(cmd, m_stopSignal, nullptr);
                    });
                    while (f.wait_for(std::chrono::milliseconds(500)) !=
                           std::future_status::ready) {
                        if (m_stopSignal) {
                            fs::remove_all(workDir);
                            return false;
                        }
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
                        if (p.path().filename().string().find("s") == 0)
                            parts.push_back(p.path());
                    std::sort(parts.begin(), parts.end());

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
                        std::string cmd = nicePrefix + "ffmpeg -v error -y -i " +
                                          ScraperUtils::Q(p.string()) + " " + baseArgs +
                                          " -progress " + ScraperUtils::Q(progP.string()) + " " +
                                          ScraperUtils::Q(outP.string()) + ScraperUtils::DEVNULL();
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
                if (MediaProbe::verifyIntegrity(finalMergedInWork.string(), duration,
                                                m_stopSignal)) {

                    m_progressCallback(seriesName, "Salvataggio...");

                    std::error_code ec;
                    fs::rename(finalMergedInWork, inputPath, ec);

                    if (ec) {
                        fs::copy(finalMergedInWork, inputPath, fs::copy_options::overwrite_existing,
                                 ec);
                        if (ec)
                            throw std::runtime_error("Impossibile copiare file sul disco finale: " +
                                                     ec.message());
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
