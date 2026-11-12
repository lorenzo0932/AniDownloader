#include "core/ExecutionEngine.hpp"
#include "core/Logger.hpp"
#include "core/MediaProcessor.hpp"
#include "core/PipelineCoordinator.hpp"
#include "core/PipelineJobStore.hpp"
#include "core/PlanningService.hpp"
#include "core/SeriesIdentity.hpp"
#include "scrapers/ScraperUtils.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

namespace Core {

    namespace {
        // Chiude sempre il run, anche su uscita anticipata o eccezione: lo
        // stato è deciso dai job del run (falliti/interrotti) e dallo stop.
        class RunGuard {
          public:
            RunGuard(PipelineJobStore& store, std::string id, const std::atomic<bool>& stop)
                : m_store(store), m_id(std::move(id)), m_stop(stop) {}

            RunGuard(const RunGuard&) = delete;
            RunGuard& operator=(const RunGuard&) = delete;

            ~RunGuard() {
                if (m_id.empty())
                    return;
                std::string stato;
                if (m_stop.load() || std::uncaught_exceptions() > 0) {
                    stato = "interrotto";
                } else {
                    const auto riepilogo = m_store.runSummary(m_id);
                    // Anche i job rimasti in retry_wait (es. conversione
                    // fallita e riprogrammata) rendono il run "con errori":
                    // chiuderlo come "completato" nasconde il problema.
                    const bool problemi =
                        riepilogo && (riepilogo->nonRiusciti > 0 || riepilogo->interrotti > 0 ||
                                      riepilogo->inCorso > 0);
                    stato = problemi ? "completato_con_errori" : "completato";
                }
                if (!m_store.finishRun(m_id, stato))
                    Core::Logger::warn("Chiusura run " + m_id + " fallita: " + m_store.lastError());
            }

          private:
            PipelineJobStore& m_store;
            std::string m_id;
            const std::atomic<bool>& m_stop;
        };
    } // namespace

    ExecutionEngine::ExecutionEngine(Config::AppConfigManager& config) : m_config(config) {}

    struct PlanningResult {
        std::mutex mutex;
        std::condition_variable cv;
        std::vector<std::pair<Series, DownloadTask>> tasks;
        std::vector<std::string> errors;
    };

    void ExecutionEngine::setPipeline(PipelineJobStore* store, std::string origin,
                                      std::function<void(const std::string&, int)> onCompleted) {
        m_store = store;
        m_origin = std::move(origin);
        m_onCompleted = std::move(onCompleted);
    }

    void ExecutionEngine::runPipeline(const std::vector<Series>& seriesList, bool burstMode,
                                      std::atomic<bool>& stopSignal, ProgressCb onProgress,
                                      const StatusCb& onStatus, const FinishedCb& onTaskFinished,
                                      const SkippedCb& onTaskSkipped,
                                      const AnalysisCb& onAnalysisDone, ActivityCb onActivity) {
        PipelineJobStore& store = *m_store;
        const int orfani = store.resetOrfani();
        if (orfani < 0)
            Core::Logger::warn("Reset job orfani della pipeline fallito: " + store.lastError());
        else if (orfani > 0)
            Core::Logger::info(std::format("Ripristinati {} job orfani della pipeline", orfani));

        // Un solo esecutore (PipelineGate + InstanceLock): i run rimasti
        // aperti sono di un processo precedente terminato male.
        const int runOrfani = store.closeOrphanRuns();
        if (runOrfani < 0)
            Core::Logger::warn("Chiusura run orfani fallita: " + store.lastError());
        else if (runOrfani > 0)
            Core::Logger::info(std::format("Chiusi {} run orfani", runOrfani));

        // Workdir/publish temp lasciati da un SIGKILL. Qui nessuna conversione
        // è attiva (PipelineGate + InstanceLock), quindi sono tutti orfani.
        std::vector<std::string> seriePaths;
        seriePaths.reserve(seriesList.size());
        for (const auto& s : seriesList)
            seriePaths.push_back(s.path);
        MediaProcessor::cleanupOrphanWorkDirs(seriePaths);

        const std::string runId = store.beginRun(m_origin);
        if (runId.empty())
            Core::Logger::warn("Apertura run fallita: " + store.lastError());
        RunGuard runGuard(store, runId, stopSignal);

        onStatus("Analisi e download in parallelo...");

        auto strategy = m_config.getExecutionStrategy(seriesList.size(), burstMode);
        auto attivi = [&store]() {
            const auto c = store.counts();
            auto g = [&c](const char* k) {
                const auto it = c.find(k);
                return it == c.end() ? 0 : it->second;
            };
            return g("queued_download") + g("downloading") + g("downloaded_waiting_conversion") +
                   g("converting");
        };

        // I pool sono del PipelineCoordinator: il run inietta solo il contesto
        // (progress, activity, stop, strategy, completamento).
        PipelineRunContext ctx;
        ctx.strategy = strategy;
        ctx.stop = &stopSignal;
        ctx.onProgress = onProgress;
        ctx.onActivity = onActivity;
        ctx.onCompleted = m_onCompleted;
        ctx.onFinished = [onTaskFinished](const std::string& name, int ep, double dlTime,
                                          double convTime) {
            if (onTaskFinished)
                onTaskFinished({.name = name,
                                .success = true,
                                .episodeNumber = ep,
                                .dlTime = dlTime,
                                .convTime = convTime,
                                .error = ""});
        };
        const int dlWorkers = (std::min)(static_cast<int>(seriesList.size()),
                                         (std::max)(strategy.maxConcurrentTasks, 2));
        PipelineCoordinator coordinatore(m_store->dbPath(), dlWorkers, 2);
        coordinatore.start(ctx);

        // Pianificazione: appena una serie è pronta, i job entrano in coda.
        auto pending = std::make_shared<std::atomic<int>>(0);
        std::vector<std::thread> analysis;
        analysis.reserve(seriesList.size());
        for (const auto& s : seriesList) {
            if (stopSignal)
                break;
            (*pending)++;
            analysis.emplace_back([&, s, runId]() {
                Series sc = s;
                sc.path = ScraperUtils::expandTilde(s.path);
                auto tasks = PlanningService::planSingleSeries(sc, [&](const std::string& stage) {
                    if (onProgress && !stopSignal)
                        onProgress(s.name, 0, stage);
                });
                if (stopSignal) {
                    (*pending)--;
                    return;
                }
                bool hasWork = false;
                bool anyError = false;
                for (const auto& t : tasks) {
                    if (t.shouldProcess) {
                        nlohmann::json payload = {{"videoUrl", t.videoUrl},
                                                  {"fileName", t.fileName}};
                        const auto key = seriesIdentity(s.service, s.seriesPageUrl) + "#" +
                                         std::to_string(t.episodeNumber);
                        // Conversione locale (videoUrl vuoto): il file è già
                        // su disco. Va direttamente alla fase conversione,
                        // saltando il pool download (che con convert=false
                        // vedrebbe il file esistente e lo darebbe completato).
                        const bool conversioneLocale = t.videoUrl.empty();
                        const std::string sourcePath =
                            conversioneLocale
                                ? (std::filesystem::path(sc.path) / t.fileName).string()
                                : "";
                        store.enqueue(
                            key, s.name, t.episodeNumber, m_origin,
                            m_origin == "interactive" ? 100 : 0, payload.dump(), sc.path, runId,
                            conversioneLocale ? "downloaded_waiting_conversion" : "queued_download",
                            sourcePath);
                        hasWork = true;
                    } else if (!t.errorMessage.empty()) {
                        anyError = true;
                        if (onProgress)
                            onProgress(s.name, 0, "❌ Errore: " + t.errorMessage);
                        if (onActivity)
                            onActivity(s.name, 0, {TaskPhase::Error, -1, t.errorMessage});
                    }
                }
                if (!hasWork && !anyError && onTaskSkipped) {
                    if (!runId.empty())
                        store.incrementSkipped(runId);
                    onTaskSkipped(s.name, "Già aggiornata");
                }
                (*pending)--;
            });
        }

        while (*pending > 0 && !stopSignal)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // I thread di analisi usano riferimenti allo stack di runPipeline
        // (onProgress, onActivity, store, ...): su stop vanno comunque
        // joinati. Un detach lascerebbe use-after-free quando il chiamante
        // distrugge il PipelineJobStore e i callback.
        for (auto& t : analysis)
            t.join();
        if (onAnalysisDone && !stopSignal)
            onAnalysisDone();

        if (!stopSignal) {
            while (attivi() > 0 && !stopSignal)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        coordinatore.stop();
        onStatus(stopSignal ? "Processo interrotto." : "Elaborazione completata.");
    }

    void ExecutionEngine::run(const std::vector<Series>& seriesList, bool burstMode,
                              std::atomic<bool>& stopSignal, ProgressCb onProgress,
                              const StatusCb& onStatus, const FinishedCb& onTaskFinished,
                              const SkippedCb& onTaskSkipped, const AnalysisCb& onAnalysisDone,
                              ActivityCb onActivity) {
        if (m_store != nullptr) {
            runPipeline(seriesList, burstMode, stopSignal, std::move(onProgress), onStatus,
                        onTaskFinished, onTaskSkipped, onAnalysisDone, std::move(onActivity));
            return;
        }
        onStatus("Analisi parallelizzata in corso...");

        auto result = std::make_shared<PlanningResult>();
        auto pending = std::make_shared<std::atomic<int>>(0);

        // jthread (C++20): join RAII al termine naturale della fase di analisi.
        // Su libc++ Apple (Xcode <= 16) std::jthread non è esposto: si ripiega
        // su std::thread con join/detach espliciti (stessa semantica).
#ifdef __cpp_lib_jthread
        using AnalysisThread = std::jthread;
#else
        using AnalysisThread = std::thread;
#endif
        std::vector<AnalysisThread> analysisThreads;
        analysisThreads.reserve(seriesList.size());

        for (const auto& s : seriesList) {
            if (stopSignal)
                break;
            (*pending)++;

            // Callback copiate per valore: su stop questi thread vengono
            // detachati (sotto) e possono sopravvivere a run(); con i
            // riferimenti ai frame morti il broadcast SSE leggeva un this
            // corrotto (SIGSEGV). Il check su stopSignal li rende muti
            // subito dopo lo stop, senza eventi tardivi.
            analysisThreads.emplace_back([&stopSignal, result, pending, s, onProgress,
                                          onTaskSkipped]() {
                if (stopSignal) {
                    (*pending)--;
                    return;
                }

                Series seriesCopy = s;
                seriesCopy.path = ScraperUtils::expandTilde(s.path);
                auto tasks = PlanningService::planSingleSeries(
                    seriesCopy, [onProgress, &stopSignal, name = s.name](const std::string& stage) {
                        if (onProgress && !stopSignal)
                            onProgress(name, 0, stage);
                    });

                if (stopSignal) {
                    (*pending)--;
                    return;
                }

                bool hasWork = false;
                bool anyError = false;

                {
                    std::lock_guard<std::mutex> lock(result->mutex);
                    for (const auto& t : tasks) {
                        if (t.shouldProcess) {
                            result->tasks.push_back({seriesCopy, t});
                            hasWork = true;
                        } else if (!t.errorMessage.empty()) {
                            result->errors.push_back(s.name + ": " + t.errorMessage);
                            anyError = true;
                        }
                    }
                }
                result->cv.notify_all();

                if (!hasWork && !anyError) {
                    if (onTaskSkipped)
                        onTaskSkipped(s.name, "Già aggiornata");
                }

                (*pending)--;
            });
        }

        // Pipeline B: produzione e consumo in overlap. I worker partono con la
        // prima serie pianificata e consumano i task man mano che arrivano;
        // non si attende la fine di tutte le analisi. La priorità alta resta
        // davanti tra i task già pronti (nessuna preemption).
        auto strategy = m_config.getExecutionStrategy(seriesList.size(), burstMode);
        std::atomic<size_t> elaborati{0};
        std::vector<std::future<void>> workers;
        int downloadWorkers = (std::min)(static_cast<int>(seriesList.size()),
                                         (std::max)(strategy.maxConcurrentTasks * 2, 4));
        workers.reserve(downloadWorkers);
        for (int i = 0; i < downloadWorkers; ++i) {
            workers.push_back(std::async(std::launch::async, [&]() {
                while (true) {
                    std::pair<Series, DownloadTask> item;
                    {
                        std::unique_lock<std::mutex> lock(result->mutex);
                        result->cv.wait_for(lock, std::chrono::milliseconds(50), [&] {
                            return !result->tasks.empty() || pending->load() == 0 ||
                                   stopSignal.load();
                        });
                        if (stopSignal)
                            return;
                        if (result->tasks.empty()) {
                            if (pending->load() == 0)
                                return;
                            continue;
                        }
                        auto it =
                            std::find_if(result->tasks.begin(), result->tasks.end(),
                                         [](const auto& t) { return t.first.isHighPriority; });
                        if (it == result->tasks.end())
                            it = result->tasks.begin();
                        item = std::move(*it);
                        result->tasks.erase(it);
                    }

                    if (onActivity)
                        onActivity(item.first.name, item.second.episodeNumber,
                                   {TaskPhase::Queued, -1, "In attesa di elaborazione"});

                    auto taskProgressCb = [onProgress, epNum = item.second.episodeNumber](
                                              const std::string& name, const std::string& msg) {
                        if (onProgress)
                            onProgress(name, epNum, msg);
                    };
                    MediaProcessor mp(
                        taskProgressCb, stopSignal,
                        [onActivity, name = item.first.name,
                         ep = item.second.episodeNumber](const TaskProgress& progress) {
                            if (onActivity)
                                onActivity(name, ep, progress);
                        });
                    ProcessResult res = mp.processTask(item.second, item.first, strategy);

                    // Feature 11: i parziali (.part/.part.aria2) vengono rimossi
                    // su stop SOLO se l'utente ha chiesto cleanup esplicito E il
                    // resume è disattivo. Con resume_interrupted_downloads=true
                    // (default) restano per il resume del run successivo.
                    if (!res.success && stopSignal &&
                        m_config.get<bool>("auto_cleanup_on_close", true) &&
                        !m_config.get<bool>("resume_interrupted_downloads", true)) {
                        std::string expPath = ScraperUtils::expandTilde(item.first.path);
                        std::filesystem::path partFile =
                            std::filesystem::path(expPath) / (item.second.fileName + ".part");
                        try {
                            bool removedFile = false;
                            bool removedAria2 = false;
                            if (std::filesystem::exists(partFile)) {
                                std::filesystem::remove(partFile);
                                removedFile = true;
                            }
                            if (std::filesystem::exists(partFile.string() + ".aria2")) {
                                std::filesystem::remove(partFile.string() + ".aria2");
                                removedAria2 = true;
                            }
                            if (removedFile || removedAria2) {
                                Core::Logger::info("Cleanup: rimossi file parziali per " +
                                                   item.first.name);
                            }
                        } catch (const std::exception& e) {
                            Core::Logger::error("Cleanup fallito per " + item.first.name + ": " +
                                                e.what());
                        }
                    }

                    TaskReport report{.name = item.first.name,
                                      .success = res.success,
                                      .episodeNumber = res.episodeNumber,
                                      .dlTime = res.downloadTime,
                                      .convTime = res.conversionTime,
                                      .error = res.errorMessage};
                    if (res.success) {
                        Core::Logger::result(report.name, report.dlTime, report.convTime);
                    }
                    if (onTaskFinished)
                        onTaskFinished(report);
                    elaborati.fetch_add(1);
                }
            }));
        }

        while (*pending > 0 && !stopSignal)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        {
            std::lock_guard<std::mutex> lock(result->mutex);
            for (const auto& err : result->errors) {
                auto colon = err.find(':');
                if (colon != std::string::npos && onProgress) {
                    onProgress(err.substr(0, colon), 0, "❌ Errore: " + err.substr(colon + 2));
                    if (onActivity)
                        onActivity(err.substr(0, colon), 0,
                                   {TaskPhase::Error, -1, err.substr(colon + 2)});
                }
            }
        }
        result->cv.notify_all();

        if (stopSignal) {
            // Latenza di stop storica: non attendiamo i worker in volo (I/O di
            // rete in corso = decine di secondi), li detachiamo e si fermano
            // da soli al prossimo check di stopSignal.
            for (auto& t : analysisThreads)
                t.detach();
            onStatus("Processo interrotto.");
            return;
        }

        for (auto& t : analysisThreads)
            t.join();
        if (onAnalysisDone)
            onAnalysisDone();
        for (auto& f : workers) {
            while (f.wait_for(std::chrono::milliseconds(200)) != std::future_status::ready) {
                if (stopSignal)
                    break;
            }
        }
        if (elaborati.load() == 0)
            onStatus("Tutto aggiornato.");
        else
            onStatus("Elaborazione completata.");
    }

} // namespace Core
