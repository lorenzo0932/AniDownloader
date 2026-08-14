#pragma once

#include "scrapers/BaseScraper.hpp"
#include <atomic>
#include <string>

namespace Core {

    class AnimeUScraper : public BaseScraper {
      public:
        std::vector<DownloadTask> planSeriesTask(const Series& series,
                                                 std::atomic<bool>& stopSignal,
                                                 ScraperProgressCb progressCb = nullptr) override;
    };

} // namespace Core