#pragma once

#include "core/DownloadTask.hpp"
#include "core/Series.hpp"
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace Core {

    class BaseScraper {
      public:
        using ScraperProgressCb = std::function<void(const std::string& stage)>;

        virtual ~BaseScraper() = default;
        std::vector<DownloadTask> planSeriesTask(const Series& series) {
            std::atomic<bool> dummy{false};
            return planSeriesTask(series, dummy);
        }
        virtual std::vector<DownloadTask>
        planSeriesTask(const Series& series, std::atomic<bool>& stopSignal,
                       ScraperProgressCb progressCb = nullptr) = 0;
    };

} // namespace Core