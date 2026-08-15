#include "core/ExecutionEngine.hpp"
#include "core/Logger.hpp"
#include "core/PlanningService.hpp"
#include "scrapers/ScraperUtils.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <thread>

namespace Core {

    ExecutionEngine::ExecutionEngine(Config::AppConfigManager& config) : m_config(config) {}

    struct PlanningResult {
        std::mutex mutex;
        std::vector<std::pair<Series, DownloadTask>> tasks;
        std::vector<std::string> errors;
    };

    void ExecutionEngine::run(const std::vector<Series>& seriesList, bool burstMode,
                              std::atomic<bool>& stopSignal, ProgressCb onProgress,
                              const StatusCb& onStatus, FinishedCb onTaskFinished, SkippedCb onTaskSkipped,
                              const AnalysisCb& onAnalysisDone) {
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

            analysisThreads.emplace_back(
                [&stopSignal, result, pending, s, &onProgress, &onTaskSkipped]() {
                    if (stopSignal) {
                        (*pending)--;
                        return;
                    }

                    Series seriesCopy = s;
                    seriesCopy.path = ScraperUtils::expandTilde(s.path);
                    auto tasks = PlanningService::planSingleSeries(
                        seriesCopy, [&onProgress, name = s.name](const std::string& stage) {
                            if (onProgress)
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

                    if (!hasWork && !anyError) {
                        if (onTaskSkipped)
                            onTaskSkipped(s.name, "Già aggiornata");
                    }

                    (*pending)--;
                });
        }

        while (*pending > 0 && !stopSignal) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        std::vector<std::pair<Series, DownloadTask>> toProcess;
        {
            std::lock_guard<std::mutex> lock(result->mutex);
            toProcess = std::move(result->tasks);
            for (const auto& err : result->errors) {
                auto colon = err.find(':');
                if (colon != std::string::npos && onProgress) {
                    onProgress(err.substr(0, colon), 0, "❌ Errore: " + err.substr(colon + 2));
                }
            }
        }

        if (stopSignal) {
            // Latenza di stop storica: non attendiamo i worker in volo (I/O di
            // rete in corso = decine di secondi), li detachiamo e si fermano
            // da soli al prossimo check di stopSignal.
            for (auto& t : analysisThreads)
                t.detach();
            onStatus("Processo interrotto.");
            return;
        }

        // Percorso normale: pending == 0 ⇒ worker già terminati. Join esplicito
        // (obbligatorio per std::thread prima del distruttore, no-op per jthread).
        for (auto& t : analysisThreads)
            t.join();
        if (onAnalysisDone)
            onAnalysisDone();

        if (toProcess.empty()) {
            onStatus("Tutto aggiornato.");
            return;
        }

        std::sort(toProcess.begin(), toProcess.end(), [](const auto& a, const auto& b) {
            return a.first.isHighPriority > b.first.isHighPriority;
        });

        onStatus(std::format("Inizio elaborazione di {} serie...", toProcess.size()));
        auto strategy = m_config.getExecutionStrategy(toProcess.size(), burstMode);

        std::atomic<size_t> nextIndex(0);
        std::vector<std::future<void>> workers;
        int downloadWorkers = (std::min)(static_cast<int>(toProcess.size()),
                                         (std::max)(strategy.maxConcurrentTasks * 2, 4));
        workers.reserve(downloadWorkers);
        for (int i = 0; i < downloadWorkers; ++i) {
            workers.push_back(std::async(std::launch::async, [&]() {
                while (true) {
                    size_t idx = nextIndex.fetch_add(1);
                    if (idx >= toProcess.size() || stopSignal)
                        break;

                    auto& item = toProcess[idx];
                    auto taskProgressCb = [onProgress, epNum = item.second.episodeNumber](
                                              const std::string& name, const std::string& msg) {
                        if (onProgress)
                            onProgress(name, epNum, msg);
                    };
                    MediaProcessor mp(taskProgressCb, stopSignal);
                    ProcessResult res = mp.processTask(item.second, item.first, strategy);

                    // Feature 11: i parziali (.part/.part.aria2) vengono rimossi
                    // su stop SOLO se l'utente ha chiesto cleanup esplicito E il
                    // resume è disattivo. Con resume_interrupted_downloads=true
                    // (default) restano per il resume del run successivo.
                    if (!res.success && stopSignal &&
                        m_config.get<bool>("auto_cleanup_on_close", true) &&
                        !m_config.get<bool>("resume_interrupted_downloads", true)) {
                        std::string expPath = ScraperUtils::expandTilde(item.first.path);
                        // Feature 11: i parziali vivono in .part/.part.aria2.
                        // Il nome finale esiste solo dopo il publish (file valido):
                        // non va mai toccato in un cleanup.
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
                }
            }));
        }

        for (auto& f : workers) {
            while (f.wait_for(std::chrono::milliseconds(200)) != std::future_status::ready) {
                if (stopSignal)
                    break;
            }
            if (stopSignal)
                break;
        }
        onStatus(stopSignal ? "Processo interrotto." : "Elaborazione completata.");
    }

} // namespace Core
