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
        // File finale sul disco (valorizzato sui successi): è il contratto che
        // permette di separare download e conversione in due fasi.
        std::string filePath;
    };

    using ProgressCallback = std::function<void(const std::string&, const std::string&)>;

    class MediaProcessor {
      public:
        MediaProcessor(ProgressCallback callback, std::atomic<bool>& stopSignal,
                       TaskProgressCallback activity = {});

        ProcessResult processTask(const DownloadTask& task, const Series& series,
                                  const Config::ExecutionStrategy& strategy);

        // Fase conversioni separata (pipeline condivisa, fase 2): converte un
        // file già presente e sano. Non scarica e non tocca il file sorgente
        // se la conversione fallisce (convertAndVerify usa un temp + publish).
        bool convertFile(const std::string& inputPath, const std::string& seriesName,
                         const Config::ExecutionStrategy& strategy, double& outTime);

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
