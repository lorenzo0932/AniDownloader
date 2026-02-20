#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <future>
#include <mutex>
#include <map>
#include <atomic>
#include <sstream>
#include "core/SeriesRepository.hpp"
#include "core/PlanningService.hpp"
#include "core/MediaProcessor.hpp"
#include "core/scrapers/ScraperUtils.hpp"
#include "config/AppConfigManager.hpp"
#include "config/PathHelper.hpp"

using namespace Core;

std::mutex g_statusMutex;
std::map<std::string, std::string> g_statusMap;
auto g_startTime = std::chrono::steady_clock::now();

void displayStatus(const std::vector<std::string>& allNames) {
    std::stringstream ss;
    ss << "\033[H"; 
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - g_startTime).count();
    ss << "==========================================================\n";
    ss << "   AniDownloader C++ Dashboard | Tempo: " << elapsed << "s\n";
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

int main() {
    Config::AppConfigManager configManager;
    std::string seriesJsonPath = configManager.get<std::string>("json_file_path", Config::PathHelper::getSeriesJsonPath().string());
    bool convert = configManager.get<bool>("convert_to_h265", true);
    int numChunks = configManager.get<int>("num_chunks", 4);

    SeriesRepository repo(seriesJsonPath);
    auto seriesList = repo.loadSeriesData();
    if (seriesList.empty()) return 0;

    std::cout << "\033[2J📡 Analisi parallela in corso..." << std::endl;
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

    std::atomic<bool> stop(false);
    std::vector<std::string> allNames;
    for (auto& p : toProcess) allNames.push_back(p.first.name);

    auto cb = [](const std::string& n, const std::string& m) {
        std::lock_guard<std::mutex> l(g_statusMutex);
        g_statusMap[n] = m;
    };

    const int MAX_CONCURRENT = 10;
    std::atomic<size_t> nextIndex(0);
    std::vector<std::future<void>> workers;

    for (int i = 0; i < MAX_CONCURRENT; ++i) {
        workers.push_back(std::async(std::launch::async, [&]() {
            while (true) {
                size_t idx = nextIndex.fetch_add(1);
                if (idx >= toProcess.size() || stop) break;
                MediaProcessor mp(cb, stop);
                mp.processTask(toProcess[idx].second, toProcess[idx].first, convert, numChunks);
            }
        }));
    }

    while (true) {
        displayStatus(allNames);
        bool allDone = true;
        for (auto& f : workers) if (f.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) allDone = false;
        if (allDone) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    displayStatus(allNames);
    std::cout << "\n🏁 Fine attività.\n";
    return 0;
}