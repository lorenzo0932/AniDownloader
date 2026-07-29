#pragma once

#include "core/Series.hpp"
#include "scrapers/BaseScraper.hpp"
#include <memory>
#include <string>

namespace Core {

    class PlanningService {
    public:
        static std::unique_ptr<BaseScraper> getScraperInstance(const std::string& serviceName);

        static std::vector<DownloadTask> planSingleSeries(const Series& series,
            BaseScraper::ScraperProgressCb progressCb = nullptr);
    };

}