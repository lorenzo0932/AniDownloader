#pragma once

#include "config/AppConfigManager.hpp"
#include "core/MediaProcessor.hpp"
#include "core/Series.hpp"
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace Core {

    struct TaskReport {
        std::string name;
        bool success;
        int episodeNumber = -1;
        double dlTime;
        double convTime;
        std::string error;
    };

    class ExecutionEngine {
      public:
        using ProgressCb =
            std::function<void(const std::string& name, int episode, const std::string& msg)>;
        using StatusCb = std::function<void(const std::string& overallStatus)>;
        using FinishedCb = std::function<void(const TaskReport& report)>;
        using SkippedCb =
            std::function<void(const std::string& name, const std::string& reason)>; // NUOVO
        using AnalysisCb = std::function<void()>;

        explicit ExecutionEngine(Config::AppConfigManager& config);

        void run(const std::vector<Series>& seriesList, bool burstMode,
                 std::atomic<bool>& stopSignal, ProgressCb onProgress, const StatusCb& onStatus,
                 FinishedCb onTaskFinished,
                 SkippedCb onTaskSkipped, // AGGIUNTO
                 const AnalysisCb& onAnalysisDone);

      private:
        Config::AppConfigManager& m_config;
    };
} // namespace Core