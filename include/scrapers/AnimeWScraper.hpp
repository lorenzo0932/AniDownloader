#pragma once

#include "scrapers/BaseScraper.hpp"
#include <atomic>
#include <string>
#include <vector>

namespace Core {

    struct EpisodeCandidate {
        int episodeNumber;
        std::string episodeUrl;
    };

    class AnimeWScraper : public BaseScraper {
      public:
        std::vector<DownloadTask> planSeriesTask(const Series& series,
                                                 std::atomic<bool>& stopSignal,
                                                 ScraperProgressCb progressCb = nullptr) override;
        std::vector<EpisodeCandidate> getCandidates(const Series& series);
    };

} // namespace Core