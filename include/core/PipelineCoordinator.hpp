#pragma once

#include "config/AppConfigManager.hpp"
#include "core/PipelineJobStore.hpp"
#include "core/TaskProgress.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <thread>
#include <vector>

// Coordinatore della pipeline condivisa (fase 2): due pool separati sopra la
// coda per-episodio. Il pool download porta queued_download ->
// downloaded_waiting_conversion e termina lì (non aspetta una conversione);
// il pool conversione consuma solo sorgenti già complete. Il budget globale
// di conversione è il numero di worker del pool (default 2 episodi), non un
// limite per origine.
namespace Core {

    // Contesto di un run: i pool del coordinator lavorano con i callback del
    // run chiamante (progress, activity, stop, strategy, completamento).
    struct PipelineRunContext {
        Config::ExecutionStrategy strategy;
        std::atomic<bool>* stop{nullptr};
        std::function<void(const std::string&, int, const std::string&)> onProgress;
        std::function<void(const std::string&, int, const TaskProgress&)> onActivity;
        std::function<void(const std::string&, int, double, double)> onFinished;
        std::function<void(const std::string&, int)> onCompleted;
    };

    class PipelineCoordinator {
      public:
        // true = handler riuscito (il coordinatore avanza la fase); false =
        // errore, il coordinatore applica retry con backoff o failed.
        using Handler = std::function<bool(const PipelineJob&)>;

        PipelineCoordinator(std::filesystem::path dbPath, int downloadThreads = 1,
                            int conversionThreads = 2);
        PipelineCoordinator(const PipelineCoordinator&) = delete;
        PipelineCoordinator& operator=(const PipelineCoordinator&) = delete;
        ~PipelineCoordinator();

        void setDownloadHandler(Handler handler);
        void setConversionHandler(Handler handler);

        void start();
        void start(const PipelineRunContext& ctx);
        void stop();

        [[nodiscard]] bool attivo() const noexcept;
        struct Stato {
            bool attivo{false};
            int downloadWorker{0};
            int conversionWorker{0};
        };
        [[nodiscard]] Stato stato() const;
        [[nodiscard]] std::map<std::string, int> counts() const;

        // Backoff per tentativo (secondi): 5', 15', 1h; poi failed.
        static constexpr std::int64_t kBackoffSecondi[] = {300, 900, 3600};

      private:
        void cicloDownload();
        void cicloConversione();
        void cicloDownloadCtx();
        void cicloConversioneCtx();
        void esegui(PipelineJobStore& store, const PipelineJob& job, const Handler& handler,
                    const std::string& faseCorrente);

        std::filesystem::path m_dbPath;
        int m_downloadThreads{1};
        int m_conversionThreads{2};
        Handler m_downloadHandler;
        Handler m_conversionHandler;
        std::vector<std::thread> m_threads;
        std::atomic<bool> m_attivo{false};
        PipelineRunContext m_ctx;
        bool m_usaCtx{false};
    };

} // namespace Core
