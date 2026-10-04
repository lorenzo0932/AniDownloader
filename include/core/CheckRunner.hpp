#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

// Un giro completo check → download → commit stato (B4, ADR-004): la stessa
// pipeline che prima eseguiva solo il processo one-shot (main CLI / timer).
// Usato dalla CLI manuale e dal worker dello Scheduler — un solo punto di
// verità per "cosa significa fare un giro".
namespace Config {
    class AppConfigManager;
}

namespace Core {
    struct TaskReport;

    struct CheckOutcome {
        enum class Salto { Nessuno, Vuoto, Occupato };
        bool eseguito{false}; // false = giro saltato (lock occupato o zero serie)
        Salto salto{Salto::Nessuno};
        std::string nota; // motivo del salto o riepilogo del giro
        std::vector<TaskReport> reports;
    };

    class CheckRunner {
      public:
        // jsonPath vuoto = default da config. burst = dashboard ANSI (solo
        // CLI interattiva; il demone passa false). stop = abort cooperativo
        // (Ctrl+C in CLI; lo Scheduler non lo imposta mai: il giro in corso
        // non si interrompe, 2-bis). Mai eccezioni oltre il confine.
        static CheckOutcome
        runOnce(Config::AppConfigManager& config, const std::string& jsonPath, bool burst,
                std::atomic<bool>& stop,
                std::function<void(const std::string&, int, const std::string&)> onProgress = {},
                std::function<void(const std::string&)> onOverall = {});

      private:
        CheckRunner() = delete;
    };

} // namespace Core
