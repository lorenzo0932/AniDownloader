#pragma once

#include "scrapers/BaseScraper.hpp"
#include <string>
#include <atomic>

namespace Core {

    class AnimeUScraper : public BaseScraper {
    public:
        std::vector<DownloadTask> planSeriesTask(const Series& series, std::atomic<bool>& stopSignal,
            ScraperProgressCb progressCb = nullptr) override;
    };

}