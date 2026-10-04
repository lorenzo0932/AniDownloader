#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

// Scheduler interno (B4, ADR-004 §2 + 2-bis): sostituisce i timer OS.
// Un solo thread fa tutto (tick + worker minimo): valuta la schedulazione
// ogni tick, accoda un check quando maturo, esegue i task in coda uno alla
// volta. B6 spaccherà tick e worker in pool con priorità e backoff pieno.
// Stop cooperativo: non si accodano nuovi giri e si attende la fine di
// quello in corso — mai kill, mai interruzioni a metà episodio.
namespace Config {
    class AppConfigManager;
}

namespace Core {

    class Scheduler {
      public:
        static constexpr int kDefaultIntervalloMinuti = 15;
        static constexpr int kMinIntervalloMinuti = 5;
        static constexpr int kMaxIntervalloMinuti = 24 * 60;
        static constexpr int kTickSecondi = 30;
        static constexpr int kRetryOccupatoSecondi = 300;

        Scheduler(Config::AppConfigManager& config, const std::string& jsonPath);
        Scheduler(const Scheduler&) = delete;
        Scheduler& operator=(const Scheduler&) = delete;
        ~Scheduler();

        void start();
        void stop(); // attende il giro in corso, poi chiude il thread

        [[nodiscard]] bool running() const noexcept;

      private:
        void ciclo();
        // Ritorna true se un check va accodato ora (e aggiorna ultimo giro).
        bool checkMaturo(std::int64_t ora);
        void eseguiTask();

        Config::AppConfigManager& m_config;
        std::string m_jsonPath;
        std::string m_dbPath;
        std::thread m_thread;
        std::atomic<bool> m_attivo{false};
    };

} // namespace Core
