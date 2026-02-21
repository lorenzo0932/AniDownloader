#include "scrapers/AnimeUScraper.hpp"
#include "scrapers/ScraperUtils.hpp"
#include <cpr/cpr.h>
#include <regex>
#include <iostream>
#include <algorithm>

namespace Core {

DownloadTask AnimeUScraper::planSeriesTask(const Series& series) {
    DownloadTask task;
    task.shouldProcess = false;

    try {
        cpr::Response r = cpr::Get(cpr::Url{series.seriesPageUrl}, 
                                 cpr::Header{{"User-Agent", "Mozilla/5.0"}},
                                 cpr::VerifySsl{false});

        if (r.status_code != 200) return task;

        // USARE R"raw(...)raw" PER EVITARE ERRORI DI SINTASSI NELLE REGEX
        std::regex epRegex(R"raw(class="episode-item"[^>]*href="([^"]+)"[^>]*>.*?(\d+))raw");
        
        auto words_begin = std::sregex_iterator(r.text.begin(), r.text.end(), epRegex);
        auto words_end = std::sregex_iterator();

        struct Ep { int n; std::string u; };
        std::vector<Ep> eps;

        for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
            std::smatch match = *i;
            eps.push_back({std::stoi(match[2].str()), match[1].str()});
        }

        if (eps.empty()) return task;

        std::sort(eps.begin(), eps.end(), [](const Ep& a, const Ep& b) { return a.n < b.n; });

        int next = ScraperUtils::getNextEpisodeNum(series.path);
        for (const auto& ep : eps) {
            int local = series.continueSeries ? (ep.n + series.passedEpisodes) : ep.n;
            if (local >= next) {
                task.videoUrl = ep.u;
                task.episodeNumber = local;
                task.shouldProcess = true;
                task.fileName = series.name + "_Ep_" + std::to_string(local) + ".mp4";
                break;
            }
        }
    } catch (const std::exception& e) {
        task.errorMessage = e.what();
    }

    return task;
}

}