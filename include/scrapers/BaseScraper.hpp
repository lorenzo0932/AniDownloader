#pragma once

#include "core/Series.hpp"
#include <string>
#include <vector>
#include <atomic>
#include <functional>

namespace Core {

    struct DownloadTask {
        bool shouldProcess = false;
        std::string videoUrl;
        int episodeNumber = -1;
        std::string fileName;
        std::string errorMessage;
    };

    class BaseScraper {
    public:
        using ScraperProgressCb = std::function<void(const std::string& stage)>;

        virtual ~BaseScraper() = default;
        std::vector<DownloadTask> planSeriesTask(const Series& series) {
            std::atomic<bool> dummy{false};
            return planSeriesTask(series, dummy);
        }
        virtual std::vector<DownloadTask> planSeriesTask(const Series& series, std::atomic<bool>& stopSignal,
            ScraperProgressCb progressCb = nullptr) = 0;
    };

}