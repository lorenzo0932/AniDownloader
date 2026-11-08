#include "core/ExecutionEngine.hpp"
#include "core/Logger.hpp"
#include "core/PipelineJobStore.hpp"
#include "core/PlanningService.hpp"
#include "core/SeriesIdentity.hpp"
#include "scrapers/ScraperUtils.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

namespace Core {

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
                                      const StatusCb& onStatus, FinishedCb onTaskFinished,
                                      const SkippedCb& onTaskSkipped,
                                      const AnalysisCb& onAnalysisDone, ActivityCb onActivity) {
        PipelineJobStore& store = *m_store;
        onStatus("Analisi e download in parallelo...");

        auto strategy = m_config.getExecutionStrategy(seriesList.size(), burstMode);
        auto strategyDl = strategy;
        strategyDl.convertToH265 = false;

        auto attivi = [&store]() {
            const auto c = store.counts();
            auto g = [&c](const char* k) {
                const auto it = c.find(k);
                return it == c.end() ? 0 : it->second;
            };
            return g("queued_download") + g("downloading") + g("downloaded_waiting_conversion") +
                   g("converting");
        };
        auto retryOrFail = [](PipelineJobStore& st, const PipelineJob& job,
                              const std::string& errore) {
            static const std::int64_t backoff[] = {300, 900, 3600};
            if (job.tentativi >= PipelineJobStore::kMaxTentativi) {
                st.fail(job.id, errore);
                return;
            }
            const auto i = static_cast<std::size_t>(
                (std::min)(job.tentativi, static_cast<int>(std::size(backoff)) - 1));
            st.retry(job.id, backoff[i], errore);
        };

        std::atomic<bool> chiudi{false};
        std::vector<std::future<void>> pools;

        // Pool conversione (budget globale = 2 episodi).
        for (int i = 0; i < 2; ++i) {
            pools.push_back(std::async(std::launch::async, [&]() {
                while (true) {
                    auto job = store.claim("downloaded_waiting_conversion", "converting");
                    if (!job.has_value()) {
                        if (chiudi.load() && attivi() == 0)
                            return;
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }
                    Series serie;
                    serie.name = job->serie;
                    serie.path = job->seriePath;
                    auto progressCb = [onProgress, ep = job->episodio](const std::string& n,
                                                                       const std::string& m) {
                        if (onProgress)
                            onProgress(n, ep, m);
                    };
                    MediaProcessor mp(
                        progressCb, stopSignal,
                        [onActivity, n = job->serie, ep = job->episodio](const TaskProgress& p) {
                            if (onActivity)
                                onActivity(n, ep, p);
                        });
                    double convTime = 0.0;
                    const bool ok = !job->filePath.empty() &&
                                    mp.convertFile(job->filePath, job->serie, strategy, convTime);
                    if (ok) {
                        store.transition(job->id, "converting", "completed");
                        if (m_onCompleted)
                            m_onCompleted(job->serie, job->episodio);
                        if (onTaskFinished)
                            onTaskFinished({.name = job->serie,
                                            .success = true,
                                            .episodeNumber = job->episodio,
                                            .dlTime = 0.0,
                                            .convTime = convTime,
                                            .error = ""});
                    } else {
                        retryOrFail(store, *job, "conversione fallita");
                    }
                }
            }));
        }

        // Pianificazione: appena una serie è pronta, i suoi job entrano in
        // coda e i pool partono (variante B).
        auto pending = std::make_shared<std::atomic<int>>(0);
        std::vector<std::thread> analysis;
        analysis.reserve(seriesList.size());
        for (const auto& s : seriesList) {
            if (stopSignal)
                break;
            (*pending)++;
            analysis.emplace_back([&, s]() {
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
                        store.enqueue(key, s.name, t.episodeNumber, m_origin,
                                      m_origin == "interactive" ? 100 : 0, payload.dump(), sc.path);
                        hasWork = true;
                    } else if (!t.errorMessage.empty()) {
                        anyError = true;
                        if (onProgress)
                            onProgress(s.name, 0, "❌ Errore: " + t.errorMessage);
                        if (onActivity)
                            onActivity(s.name, 0, {TaskPhase::Error, -1, t.errorMessage});
                    }
                }
                if (!hasWork && !anyError && onTaskSkipped)
                    onTaskSkipped(s.name, "Già aggiornata");
                (*pending)--;
            });
        }

        // Pool download: scarica e pubblica; la conversione è del secondo pool.
        std::vector<std::future<void>> dl;
        int dlWorkers = (std::min)(static_cast<int>(seriesList.size()),
                                   (std::max)(strategy.maxConcurrentTasks, 2));
        for (int i = 0; i < dlWorkers; ++i) {
            dl.push_back(std::async(std::launch::async, [&]() {
                while (true) {
                    auto job = store.claim("queued_download", "downloading");
                    if (!job.has_value()) {
                        if (chiudi.load() && attivi() == 0)
                            return;
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }
                    DownloadTask task;
                    task.shouldProcess = true;
                    task.episodeNumber = job->episodio;
                    try {
                        const auto payload = nlohmann::json::parse(job->payload);
                        task.videoUrl = payload.value("videoUrl", "");
                        task.fileName = payload.value("fileName", "");
                    } catch (const std::exception&) {
                        retryOrFail(store, *job, "payload job illeggibile");
                        continue;
                    }
                    Series serie;
                    serie.name = job->serie;
                    serie.path = job->seriePath;
                    auto progressCb = [onProgress, ep = job->episodio](const std::string& n,
                                                                       const std::string& m) {
                        if (onProgress)
                            onProgress(n, ep, m);
                    };
                    MediaProcessor mp(
                        progressCb, stopSignal,
                        [onActivity, n = job->serie, ep = job->episodio](const TaskProgress& p) {
                            if (onActivity)
                                onActivity(n, ep, p);
                        });
                    ProcessResult res = mp.processTask(task, serie, strategyDl);
                    if (res.success) {
                        store.setFilePath(job->id, res.filePath);
                        if (strategy.convertToH265) {
                            store.transition(job->id, "downloading",
                                             "downloaded_waiting_conversion");
                        } else {
                            // Conversione OFF: il job è completo dopo il download.
                            store.transition(job->id, "downloading", "completed");
                            if (m_onCompleted)
                                m_onCompleted(job->serie, job->episodio);
                            if (onTaskFinished)
                                onTaskFinished({.name = job->serie,
                                                .success = true,
                                                .episodeNumber = job->episodio,
                                                .dlTime = res.downloadTime,
                                                .convTime = 0.0,
                                                .error = ""});
                        }
                    } else {
                        retryOrFail(store, *job, res.errorMessage);
                    }
                }
            }));
        }

        while (*pending > 0 && !stopSignal)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        for (auto& t : analysis) {
            if (stopSignal)
                t.detach();
            else
                t.join();
        }
        if (onAnalysisDone && !stopSignal)
            onAnalysisDone();

        if (!stopSignal) {
            while (attivi() > 0 && !stopSignal)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        chiudi.store(true);
        for (auto& f : dl) {
            while (f.wait_for(std::chrono::milliseconds(200)) != std::future_status::ready) {
                if (stopSignal)
                    break;
            }
        }
        for (auto& f : pools)
            f.wait();
        onStatus(stopSignal ? "Processo interrotto." : "Elaborazione completata.");
    }

    void ExecutionEngine::run(const std::vector<Series>& seriesList, bool burstMode,
                              std::atomic<bool>& stopSignal, ProgressCb onProgress,
                              const StatusCb& onStatus, FinishedCb onTaskFinished,
                              const SkippedCb& onTaskSkipped, const AnalysisCb& onAnalysisDone,
                              ActivityCb onActivity) {
        if (m_store != nullptr) {
            runPipeline(seriesList, burstMode, stopSignal, std::move(onProgress), onStatus,
                        std::move(onTaskFinished), onTaskSkipped, onAnalysisDone,
                        std::move(onActivity));
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
