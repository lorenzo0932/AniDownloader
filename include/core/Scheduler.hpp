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
    class PipelineGate;
    class SeriesRepository;

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

        // Episodio scaricato dal giro periodico: lo Scheduler lo persiste in
        // notifications e poi lo inoltra (id = riga creata, 0 se il salvataggio
        // è fallito). Il WebServer usa questo aggancio per l'SSE.
        using NotificaCb =
            std::function<void(const std::string& serie, int episodio, std::int64_t id)>;
        void onEpisodioPronto(NotificaCb cb);

        // Coordinatore del demone: se impostato, ogni check acquisisce il
        // turno in background (l'interattivo ha precedenza). Prima di start().
        void setGate(PipelineGate* gate) noexcept;

        // Condivide il repository del demone (web + scheduler stessa istanza:
        // un solo mutex sulle serie). Chiamare prima di start(); mai dopo.
        // Senza wiring il giro crea un'istanza propria (CLI/test).
        void useRepository(SeriesRepository* repo) noexcept;

        [[nodiscard]] bool running() const noexcept;
        [[nodiscard]] SchedulerStato stato() const;

      private:
        void ciclo();
        // Ritorna true se un check va accodato ora (e aggiorna ultimo giro).
        bool checkMaturo(std::int64_t ora);

        Config::AppConfigManager& m_config;
        std::string m_jsonPath;
        std::string m_dbPath;
        SeriesRepository* m_repo{nullptr};
        PipelineGate* m_gate{nullptr};
        WorkerPool m_pool;
        std::function<void(const std::string& tipo, const QueueTask& task)> m_onEvento;
        NotificaCb m_onEpisodioPronto;
        std::thread m_thread;
        std::atomic<bool> m_attivo{false};
        // Flag di ARRESTO (true = interrompi), passato a PipelineGate::acquire.
        // NON usare m_attivo: quello è true mentre lo scheduler gira e la
        // polarità del gate è opposta (era il bug dei check sempre rimandati).
        std::atomic<bool> m_abort{false};
        std::atomic<bool> m_pausa{false};
    };

} // namespace Core
