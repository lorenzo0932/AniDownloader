#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

// Scheduler interno (B4/B6, ADR-004 §2 + 2-bis): sostituisce i timer OS.
// Il tick valuta la schedulazione e accoda check; l'esecuzione è del
// WorkerPool (1 thread in B4, pool piena in B6). Stop cooperativo: niente
// nuovi giri e attesa di quello in corso — mai kill, mai interruzioni.
namespace Config {
    class AppConfigManager;
}

#include "core/WorkerPool.hpp"

namespace Core {
    struct QueueTask;

    struct SchedulerStato {
        bool attivo{false};
        bool inPausa{false};
        std::int64_t checkInCoda{0};
        std::int64_t ultimoGiro{0};
    };

    class Scheduler {
      public:
        static constexpr int kDefaultIntervalloMinuti = 15;
        static constexpr int kMinIntervalloMinuti = 5;
        static constexpr int kMaxIntervalloMinuti = 24 * 60;
        static constexpr int kTickSecondi = 30;
        // Un solo thread di esecuzione in B4: i check sono globali e il
        // lock ne fa passare uno alla volta. B-future: N per kind.
        static constexpr int kWorkerThread = 1;

        Scheduler(Config::AppConfigManager& config, const std::string& jsonPath);
        Scheduler(const Scheduler&) = delete;
        Scheduler& operator=(const Scheduler&) = delete;
        ~Scheduler();

        void start();
        void stop(); // attende giro in corso, poi chiude tick + worker

        void pausa(bool inPausa);
        void onEvento(std::function<void(const std::string& tipo, const QueueTask& task)> cb);

        [[nodiscard]] bool running() const noexcept;
        [[nodiscard]] SchedulerStato stato() const;

      private:
        void ciclo();
        // Ritorna true se un check va accodato ora (e aggiorna ultimo giro).
        bool checkMaturo(std::int64_t ora);

        Config::AppConfigManager& m_config;
        std::string m_jsonPath;
        std::string m_dbPath;
        WorkerPool m_pool;
        std::function<void(const std::string& tipo, const QueueTask& task)> m_onEvento;
        std::thread m_thread;
        std::atomic<bool> m_attivo{false};
        std::atomic<bool> m_pausa{false};
    };

} // namespace Core
