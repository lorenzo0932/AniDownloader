#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Core {
    namespace MediaProbe {

        void clearCache();

        double getVideoDuration(const std::string& filePath, std::atomic<bool>& stopSignal);
        std::string getVideoCodec(const std::string& filePath, std::atomic<bool>& stopSignal);
        bool isMediaFileHealthy(const std::string& filePath, std::atomic<bool>& stopSignal);
        bool verifyIntegrity(const std::string& filePath, double expectedDuration,
                             std::atomic<bool>& stopSignal);

    } // namespace MediaProbe
} // namespace Core
