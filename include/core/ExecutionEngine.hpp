#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include "core/Series.hpp"
#include "core/MediaProcessor.hpp"
#include "config/AppConfigManager.hpp"

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
        using ProgressCb   = std::function<void(const std::string& name, const std::string& msg)>;
        using StatusCb     = std::function<void(const std::string& overallStatus)>;
        using FinishedCb   = std::function<void(const TaskReport& report)>;
        using SkippedCb    = std::function<void(const std::string& name, const std::string& reason)>; // NUOVO
        using AnalysisCb   = std::function<void()>;

        explicit ExecutionEngine(Config::AppConfigManager& config);

        void run(const std::vector<Series>& seriesList, 
                 bool burstMode, 
                 std::atomic<bool>& stopSignal,
                 ProgressCb onProgress,
                 StatusCb onStatus,
                 FinishedCb onTaskFinished,
                 SkippedCb onTaskSkipped,     // AGGIUNTO
                 AnalysisCb onAnalysisDone);

    private:
        Config::AppConfigManager& m_config;
    };
}