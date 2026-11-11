#pragma once

#include "config/AppConfigManager.hpp"
#include "core/DownloadTask.hpp"
#include "core/Series.hpp"
#include "core/TaskProgress.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Core {

    // Stima O(1) dello spazio temporaneo necessario per convertire un input:
    // modalità chunk (split + encode + concat) ≈ 2.86×, passaggio singolo
    // ≈ 1.56× (fattore di lavoro + 30% di margine). Usa solo file_size.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::uint64_t estimateConversionBytes(std::uint64_t inputBytes, int chunksPerTask);

    // Sceglie la radice del workdir con spazio sufficiente, in ordine:
    // radici RAM → cartella della serie → cache. Le candidate devono esistere.
    // nullopt = nessuna ha `neededBytes` liberi.
    std::optional<std::filesystem::path>
    chooseWorkRoot(const std::vector<std::filesystem::path>& ramRoots,
                   const std::filesystem::path& seriesDir, const std::filesystem::path& cacheDir,
                   std::uint64_t neededBytes);

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

        // Rimuove workdir/publish temp orfani (SIGKILL) sotto le cartelle serie,
        // le radici RAM e la cache. Da chiamare solo quando nessuna conversione
        // è attiva (inizio run).
        static void cleanupOrphanWorkDirs(const std::vector<std::string>& seriesPaths);

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
