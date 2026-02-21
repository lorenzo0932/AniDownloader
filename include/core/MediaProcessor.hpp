#pragma once

#include "core/Series.hpp"
#include "core/scrapers/BaseScraper.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include <atomic>

namespace Core {

    struct ProcessResult {
        bool success = false;
        double downloadTime = 0.0;
        double conversionTime = 0.0;
        std::string errorMessage;
    };

    using ProgressCallback = std::function<void(const std::string&, const std::string&)>;

    class MediaProcessor {
    public:
        MediaProcessor(ProgressCallback callback, std::atomic<bool>& stopSignal);

        ProcessResult processTask(const DownloadTask& task, 
                                  const Series& series,
                                  bool convertToH265, 
                                  int numChunks = 1);

    private:
        ProgressCallback m_progressCallback;
        std::atomic<bool>& m_stopSignal;

        // Metodi di elaborazione
        bool convertAndVerify(const std::string& inputPath, const std::string& seriesName, 
                             int numChunks, double& outTime);
        
        // Nuovi metodi di validazione e log (Aggiunti per risolvere errori)
        bool verifyIntegrity(const std::string& filePath);
        void logError(const std::string& seriesName, const std::string& message);
        
        // Utility
        double getVideoDuration(const std::string& filePath);
        double getRamUsagePercent();
        int runCommand(const std::string& cmd, 
                       std::function<void(const std::string&)> onLineRead = nullptr);
    };

}