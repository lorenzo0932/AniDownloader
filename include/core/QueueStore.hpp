#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

// Coda persistente dei task (B4/B6, ADR-004): gli accodatori (scheduler,
// "scarica ora", CLI) scrivono righe, i worker le reclamano con claim
// atomico. Crash recovery: all'avvio gli 'attivo' orfani tornano 'attesa'.
// Ogni QueueStore apre la propria connessione (WAL + busy-timeout fanno da
// arbitri): mai condividere un handle tra thread.
namespace Core {

    struct QueueTask {
        std::int64_t id{0};
        std::string kind; // 'check' in B4; download/convert/notify da B6
        std::string serie;
        int episodio{0};
        std::string payload; // JSON libero per il worker
        int tentativi{0};
        int priorita{0};
    };

    class QueueStore {
      public:
        explicit QueueStore(const std::filesystem::path& dbPath);
        QueueStore(const QueueStore&) = delete;
        QueueStore& operator=(const QueueStore&) = delete;

        [[nodiscard]] bool isOpen() const;
        [[nodiscard]] std::string lastError() const;

        // Crash recovery: i task rimasti 'attivo' (worker morto) tornano in
        // coda. Chiamare una volta all'avvio del demone.
        bool resetOrfani();

        // Accoda; ritardoSec>0 = prossima_esecuzione nel futuro (backoff).
        // Ritorna l'id o 0 in caso d'errore.
        std::int64_t enqueue(const std::string& kind, const std::string& serie, int episodio,
                             const std::string& payload, int priorita, std::int64_t ritardoSec = 0);

        // Reclama il prossimo task eseguibile (stato attesa, scadenza
        // raggiunta; priorità poi anzianità). Atomico: due worker non
        // prendono mai lo stesso task. nullopt = coda vuota.
        std::optional<QueueTask> claim();

        bool complete(std::int64_t id);
        // Rimette in coda con backoff: tentativi+1, prossima = ora + attesa.
        bool failRetry(std::int64_t id, std::int64_t attesaSec);
        // Terminale: niente più tentativi (il chiamante logga/notifica).
        bool failDead(std::int64_t id);

        std::int64_t pendingCount(const std::string& kind = "");

      private:
        std::filesystem::path m_dbPath;
        mutable std::string m_lastError;
    };

} // namespace Core
