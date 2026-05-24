#pragma once

#include "core/Series.hpp"
#include "scrapers/BaseScraper.hpp"
#include "config/AppConfigManager.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>

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
                                  const Config::ExecutionStrategy& strategy);

        static void notifyStop(); // Sveglia tutti i thread in attesa del semaforo

        // Metodi pubblici di pre-verifica dell'integrità
        bool isMediaFileHealthy(const std::string& filePath);
        std::string getVideoCodec(const std::string& filePath);

    private:
        ProgressCallback m_progressCallback;
        std::atomic<bool>& m_stopSignal;

        // Gestione Risorse Statica (Condivisa tra tutte le istanze)
        static std::mutex s_convMutex;
        static std::condition_variable s_convCv;
        static std::atomic<int> s_activeConversions;

        bool convertAndVerify(const std::string& inputPath, const std::string& seriesName, 
                             const Config::ExecutionStrategy& strategy, double& outTime);
        
        bool verifyIntegrity(const std::string& filePath, double expectedDuration);
        void logError(const std::string& seriesName, const std::string& message);
        
        double getVideoDuration(const std::string& filePath);
        double getRamUsagePercent();
        int runCommand(const std::string& cmd, 
                       std::function<void(const std::string&)> onLineRead = nullptr);
    };

}