#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <future>
#include <mutex>
#include <map>
#include <atomic>
#include <sstream>
#include <iomanip>
#include "core/SeriesRepository.hpp"
#include "core/PlanningService.hpp"
#include "core/MediaProcessor.hpp"
#include "scrapers/ScraperUtils.hpp"
#include "config/AppConfigManager.hpp"
#include "config/PathHelper.hpp"

using namespace Core;

struct FinalStats {
    std::string name;
    double downloadTime = 0.0;
    double conversionTime = 0.0;
    std::string error;
    bool success = false;
};

std::mutex g_statusMutex;
std::mutex g_resultsMutex;
std::vector<FinalStats> g_finalResults;
std::map<std::string, std::string> g_statusMap;
auto g_startTime = std::chrono::steady_clock::now();

void displayStatus(const std::vector<std::string>& allNames, bool burst) {
    std::stringstream ss;
    ss << "\033[H"; 
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - g_startTime).count();
    ss << "==========================================================\n";
    ss << "   AniDownloader C++ | Mod: " << (burst ? "BURST 🚀" : "SILENT ☁️") << " | T: " << elapsed << "s\n";
    ss << "==========================================================\n";
    {
        std::lock_guard<std::mutex> lock(g_statusMutex);
        for (const auto& n : allNames) {
            std::string st = g_statusMap.count(n) ? g_statusMap[n] : "In attesa...";
            std::string disp = (n.length() > 34) ? n.substr(0, 31) + "..." : n;
            ss << " - " << std::left << std::setw(35) << disp << " : " << st << "          \n";
        }
    }
    ss << "==========================================================\n";
    std::cout << ss.str() << std::flush;
}

int main(int argc, char* argv[]) {
    bool burstMode = false;
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--burst") burstMode = true;

    Config::AppConfigManager configManager;
    SeriesRepository repo(configManager.get<std::string>("json_file_path", Config::PathHelper::getSeriesJsonPath().string()));
    
    auto seriesList = repo.loadSeriesData();
    if (seriesList.empty()) return 0;

    std::cout << "\033[2J📡 Analisi scrapers..." << std::endl;
    std::vector<std::future<std::pair<Series, DownloadTask>>> planning;
    for (auto s : seriesList) {
        planning.push_back(std::async(std::launch::async, [s]() mutable {
            s.path = ScraperUtils::expandTilde(s.path);
            return std::make_pair(s, PlanningService::planSingleSeries(s));
        }));
    }

    std::vector<std::pair<Series, DownloadTask>> toProcess;
    for (auto& f : planning) {
        auto res = f.get();
        if (res.second.shouldProcess) toProcess.push_back(res);
    }

    if (toProcess.empty()) { std::cout << "✅ Tutto aggiornato.\n"; return 0; }

    auto strategy = configManager.getExecutionStrategy(toProcess.size(), burstMode);
    std::atomic<bool> stop(false);
    std::vector<std::string> allNames;
    for (auto& p : toProcess) allNames.push_back(p.first.name);

    auto cb = [](const std::string& n, const std::string& m) {
        std::lock_guard<std::mutex> l(g_statusMutex);
        g_statusMap[n] = m;
    };

    // 10 worker per gestire i download massivi
    std::atomic<size_t> nextIndex(0);
    std::vector<std::future<void>> workers;
    for (int i = 0; i < 10; ++i) {
        workers.push_back(std::async(std::launch::async, [&]() {
            while (true) {
                size_t idx = nextIndex.fetch_add(1);
                if (idx >= toProcess.size() || stop) break;
                
                MediaProcessor mp(cb, stop);
                ProcessResult res = mp.processTask(toProcess[idx].second, toProcess[idx].first, strategy);
                
                if (res.success) configManager.logFinalResult(toProcess[idx].first.name, res.downloadTime, res.conversionTime);
                
                std::lock_guard<std::mutex> lock(g_resultsMutex);
                g_finalResults.push_back({toProcess[idx].first.name, res.downloadTime, res.conversionTime, res.errorMessage, res.success});
            }
        }));
    }

    while (true) {
        displayStatus(allNames, burstMode);
        bool allDone = true;
        for (auto& f : workers) if (f.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) allDone = false;
        if (allDone) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    displayStatus(allNames, burstMode);
    std::cout << "\n--- RESOCONTO FINALE ---\n";
    for (const auto& r : g_finalResults) {
        if (!r.success) std::cout << "❌ " << std::left << std::setw(35) << r.name << " | Errore: " << r.error << "\n";
        else std::cout << "✅ " << std::left << std::setw(35) << r.name << " | DL: " << r.downloadTime << "s | Conv: " << r.conversionTime << "s\n";
    }
    return 0;
}