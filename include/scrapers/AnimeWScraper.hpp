#pragma once

#include "scrapers/BaseScraper.hpp"
#include <string>
#include <vector>
#include <atomic>

namespace Core {

    struct EpisodeCandidate {
        int episodeNumber;
        std::string episodeUrl;
    };

    class AnimeWScraper : public BaseScraper {
    public:
        std::vector<DownloadTask> planSeriesTask(const Series& series, std::atomic<bool>& stopSignal) override;
        std::vector<EpisodeCandidate> getCandidates(const Series& series);
    };

}