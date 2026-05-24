#include "core/ExecutionEngine.hpp"
#include "core/PlanningService.hpp"
#include "scrapers/ScraperUtils.hpp"
#include <future>
#include <mutex>
#include <algorithm>
#include <filesystem>
#include <thread>  // Per std::this_thread::sleep_for
#include <chrono>  // Per std::chrono::milliseconds

namespace Core {

ExecutionEngine::ExecutionEngine(Config::AppConfigManager& config) : m_config(config) {}

void ExecutionEngine::run(const std::vector<Series>& seriesList, 
                         bool burstMode, 
                         std::atomic<bool>& stopSignal,
                         ProgressCb onProgress,
                         StatusCb onStatus,
                         FinishedCb onTaskFinished,
                         SkippedCb onTaskSkipped,
                         AnalysisCb onAnalysisDone) 
{
    onStatus("Analisi parallelizzata in corso...");

    // --- AVVIO AUTOMATICO CHROMEDRIVER (Se spento) ---
    // Verifica se chromedriver è già attivo nel sistema operativo.
    // Se non lo trova, lo avvia silenziosamente in background sulla porta 9515.
    #ifndef _WIN32
        std::system("pgrep -x chromedriver > /dev/null || chromedriver --port=9515 > /dev/null 2>&1 &");
        // Piccolo ritardo (400ms) per permettere al server ChromeDriver di inizializzarsi
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    #endif

    std::vector<std::pair<Series, DownloadTask>> toProcess;
    std::mutex resultsMutex;
    std::vector<std::future<void>> planningTasks;

    for (const auto& s : seriesList) {
        if (stopSignal) break;
        planningTasks.push_back(std::async(std::launch::async, [&, s]() {
            if (stopSignal) return;
            onProgress(s.name, "Analisi...");
            
            Series seriesCopy = s;
            seriesCopy.path = ScraperUtils::expandTilde(s.path);
            DownloadTask task = PlanningService::planSingleSeries(seriesCopy);
            
            std::lock_guard<std::mutex> lock(resultsMutex);
            if (task.shouldProcess) {
                toProcess.push_back({seriesCopy, task});
            } else {
                // Se lo scraper è andato in errore (es. ChromeDriver non risponde),
                // lo mostriamo chiaramente anziché nasconderlo sotto un finto "Già aggiornata"
                if (!task.errorMessage.empty()) {
                    if (onProgress) {
                        onProgress(s.name, "❌ Errore: " + task.errorMessage);
                    }
                } else {
                    if (onTaskSkipped) {
                        onTaskSkipped(s.name, "Già aggiornata");
                    }
                }
            }
        }));
    }
    for (auto& f : planningTasks) f.wait();
    if (onAnalysisDone) onAnalysisDone();

    if (toProcess.empty()) {
        onStatus("Tutto aggiornato.");
        return;
    }

    std::sort(toProcess.begin(), toProcess.end(), [](const auto& a, const auto& b) {
        return a.first.isHighPriority > b.first.isHighPriority;
    });

    onStatus("Inizio elaborazione di " + std::to_string(toProcess.size()) + " serie...");
    auto strategy = m_config.getExecutionStrategy(toProcess.size(), burstMode);

    std::atomic<size_t> nextIndex(0);
    std::vector<std::future<void>> workers;
    for (int i = 0; i < 10; ++i) {
        workers.push_back(std::async(std::launch::async, [&]() {
            while (true) {
                size_t idx = nextIndex.fetch_add(1);
                if (idx >= toProcess.size() || stopSignal) break;

                auto& item = toProcess[idx];
                MediaProcessor mp(onProgress, stopSignal);
                ProcessResult res = mp.processTask(item.second, item.first, strategy);

                // --- LOGICA DI CLEANUP AUTOMATICO ---
                if (!res.success && stopSignal && m_config.get<bool>("auto_cleanup_on_close", true)) {
                    std::string expPath = ScraperUtils::expandTilde(item.first.path);
                    std::filesystem::path fullFile = std::filesystem::path(expPath) / item.second.fileName;
                    try {
                        if (std::filesystem::exists(fullFile)) std::filesystem::remove(fullFile);
                        if (std::filesystem::exists(fullFile.string() + ".aria2")) 
                            std::filesystem::remove(fullFile.string() + ".aria2");
                    } catch (...) {}
                }

                TaskReport report{item.first.name, res.success, res.downloadTime, res.conversionTime, res.errorMessage};
                if (res.success) {
                    m_config.logFinalResult(report.name, report.dlTime, report.convTime);
                }
                if (onTaskFinished) onTaskFinished(report);
            }
        }));
    }

    for (auto& f : workers) f.wait();
    onStatus(stopSignal ? "Processo interrotto." : "Elaborazione completata.");
}

} // namespace Core