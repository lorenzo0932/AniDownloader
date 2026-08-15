#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Core {
    namespace MediaProbe {

        void clearCache();

        // True se il tool è presente nel PATH (ricerca cross-platform:
        // separatore ':' su POSIX, ';' su Windows, estensione .exe/.com/.bat).
        bool isToolAvailable(const std::string& toolName);

        double getVideoDuration(const std::string& filePath, std::atomic<bool>& stopSignal);
        std::string getVideoCodec(const std::string& filePath, std::atomic<bool>& stopSignal);
        bool isMediaFileHealthy(const std::string& filePath, std::atomic<bool>& stopSignal);
        bool verifyIntegrity(const std::string& filePath, double expectedDuration,
                             std::atomic<bool>& stopSignal);

    } // namespace MediaProbe
} // namespace Core
