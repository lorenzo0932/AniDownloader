#pragma once

#include "core/Series.hpp"
#include <string>
#include <vector>
#include <atomic>

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
        virtual ~BaseScraper() = default;
        std::vector<DownloadTask> planSeriesTask(const Series& series) {
            std::atomic<bool> dummy{false};
            return planSeriesTask(series, dummy);
        }
        virtual std::vector<DownloadTask> planSeriesTask(const Series& series, std::atomic<bool>& stopSignal) = 0;
    };

}