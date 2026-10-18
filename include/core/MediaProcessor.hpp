#pragma once

#include "config/AppConfigManager.hpp"
#include "core/DownloadTask.hpp"
#include "core/Series.hpp"
#include "core/TaskProgress.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>

namespace Core {

    struct ProcessResult {
        bool success = false;
        int episodeNumber = -1;
        double downloadTime = 0.0;
        double conversionTime = 0.0;
        std::string errorMessage;
    };

    using ProgressCallback = std::function<void(const std::string&, const std::string&)>;

    class MediaProcessor {
      public:
        MediaProcessor(ProgressCallback callback, std::atomic<bool>& stopSignal,
                       TaskProgressCallback activity = {});

        ProcessResult processTask(const DownloadTask& task, const Series& series,
                                  const Config::ExecutionStrategy& strategy);

        static void notifyStop();

      private:
        ProgressCallback m_progressCallback;
        TaskProgressCallback m_activity;
        void activity(TaskPhase phase, int percent = -1, const std::string& detail = "");
        std::atomic<bool>& m_stopSignal;

        static std::mutex s_convMutex;
        static std::condition_variable s_convCv;
        static std::atomic<int> s_activeConversions;

        bool convertAndVerify(const std::string& inputPath, const std::string& seriesName,
                              const Config::ExecutionStrategy& strategy, double& outTime);
    };

} // namespace Core
