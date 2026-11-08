#pragma once

#include "config/AppConfigManager.hpp"
#include "core/MediaProcessor.hpp"
#include "core/Series.hpp"
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace Core {

    class PipelineJobStore;

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
        using ActivityCb = std::function<void(const std::string&, int, const TaskProgress&)>;

        explicit ExecutionEngine(Config::AppConfigManager& config);

        void run(const std::vector<Series>& seriesList, bool burstMode,
                 std::atomic<bool>& stopSignal, ProgressCb onProgress, const StatusCb& onStatus,
                 const FinishedCb& onTaskFinished,
                 const SkippedCb& onTaskSkipped, // AGGIUNTO
                 const AnalysisCb& onAnalysisDone, ActivityCb onActivity = {});

        // Pipeline condivisa (fase 2): se impostata, i task pianificati vengono
        // accodati nel DB (JobKey) e lavorati in due pool download/conversione.
        void setPipeline(PipelineJobStore* store, std::string origin,
                         std::function<void(const std::string&, int)> onCompleted = {});

      private:
        void runPipeline(const std::vector<Series>& seriesList, bool burstMode,
                         std::atomic<bool>& stopSignal, ProgressCb onProgress,
                         const StatusCb& onStatus, const FinishedCb& onTaskFinished,
                         const SkippedCb& onTaskSkipped, const AnalysisCb& onAnalysisDone,
                         ActivityCb onActivity);

        Config::AppConfigManager& m_config;
        PipelineJobStore* m_store{nullptr};
        std::string m_origin{"background"};
        std::function<void(const std::string&, int)> m_onCompleted;
    };
} // namespace Core
