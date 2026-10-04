#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

// Worker pool sulla coda persistente (B6, ADR-004): N thread reclamano task
// con claim atomico ed eseguono l'handler registrato per kind. Nessun giro
// viene mai interrotto: stop() segnala e attende i task in corso (join).
// Retry con backoff progressivo 5'/15'/1h, poi fallito definitivo.
namespace Core {
    struct QueueTask;

    enum class EsitoTask { Completato, Riprova, Fallito };

    // Eventi per osservabilità (il chiamante li inoltra a SSE/log):
    // "avviato", "completato", "riprova" (con prossima esecuzione),
    // "fallito" (definitivo).
    using TaskEventoCb = std::function<void(const std::string& tipo, const QueueTask& task)>;

    using TaskHandler = std::function<EsitoTask(const QueueTask& task)>;

    class WorkerPool {
      public:
        static constexpr int kBackoffSecondi[] = {300, 900, 3600};
        static constexpr int kPollSecondi = 5;

        WorkerPool(const std::string& dbPath, int numThread);
        WorkerPool(const WorkerPool&) = delete;
        WorkerPool& operator=(const WorkerPool&) = delete;
        ~WorkerPool();

        void onKind(const std::string& kind, TaskHandler handler);
        void onEvento(TaskEventoCb cb);

        void start(); // reset orfani (crash recovery) + thread
        void stop();  // cooperativo: attende i task in corso
        void pausa(bool inPausa);

        [[nodiscard]] bool running() const noexcept;
        [[nodiscard]] bool inPausa() const noexcept;

      private:
        void ciclo();

        std::string m_dbPath;
        int m_numThread;
        std::vector<std::pair<std::string, TaskHandler>> m_handlers;
        TaskEventoCb m_onEvento;
        std::vector<std::thread> m_threads;
        std::atomic<bool> m_attivo{false};
        std::atomic<bool> m_pausa{false};
    };

} // namespace Core
