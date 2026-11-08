#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Coda per-episodio della pipeline condivisa (fase 2). Una riga per JobKey
// (identity canonica + episodio locale), con un solo job non terminale per
// chiave garantito dall'indice unico parziale nello schema v4.
// Le transizioni sono atomiche: UPDATE ... WHERE fase = <attesa>, con
// verifica di sqlite3_changes per non prendere due volte la stessa riga.
namespace Core {

    struct PipelineJob {
        std::int64_t id{0};
        std::string jobKey;
        std::string serie;
        int episodio{0};
        std::string origine; // background | interactive
        std::string fase;
        int tentativi{0};
        std::int64_t prossimaEsecuzione{0};
        int priorita{0};
        std::string errore;
        std::string payload; // JSON: videoUrl/fileName per ricostruire il task
        std::string seriePath;
        std::string filePath; // prodotto dal download, consumato dalla conversione
        std::string creatoIl;
        std::string aggiornatoIl;
    };

    class PipelineJobStore {
      public:
        explicit PipelineJobStore(std::filesystem::path dbPath);

        struct EsitoEnqueue {
            std::int64_t id{0};
            bool creato{false};
            bool promosso{false};
        };

        // Deduplica per JobKey: se esiste un job non terminale non ne crea un
        // secondo. Una richiesta interattiva promuove un job background
        // (origine + priorità alta) senza toccare la fase in corso.
        EsitoEnqueue enqueue(const std::string& jobKey, const std::string& serie, int episodio,
                             const std::string& origine, int priorita = 0,
                             const std::string& payload = "{}", const std::string& seriePath = "");

        // Claim atomico: prende il job eseguibile più prioritario nella fase
        // di partenza e lo porta a quella di arrivo. nullopt = niente pronto.
        std::optional<PipelineJob> claim(const std::string& daFase, const std::string& aFase,
                                         std::int64_t ora = 0);

        // File finale prodotto dal download (letto dal pool conversione).
        bool setFilePath(std::int64_t id, const std::string& filePath);

        // Transizione atomica condizionata alla fase corrente.
        bool transition(std::int64_t id, const std::string& daFase, const std::string& aFase,
                        const std::string& errore = "");

        // Retry con backoff: fase retry_wait e prossima = ora + delay.
        bool retry(std::int64_t id, std::int64_t delaySec, const std::string& errore = "");
        // "Riprova": rimette subito in coda un job fallito o già in attesa,
        // senza azzerare i tentativi (la storia resta leggibile).
        bool retryNow(std::int64_t id);
        // Terminale.
        bool fail(std::int64_t id, const std::string& errore = "");
        // Da retry_wait a queued_download quando la scadenza è passata.
        int promuoviRetry(std::int64_t ora = 0);

        // Recupera i job rimasti in una fase attiva per stop/crash: riporta
        // downloading -> queued_download e converting ->
        // downloaded_waiting_conversion. Le altre fasi non vengono toccate.
        // Ritorna il numero di righe ripristinate, -1 su errore.
        int resetOrfani();

        [[nodiscard]] std::vector<PipelineJob> list(const std::string& fase = "",
                                                    int limit = 100) const;
        [[nodiscard]] std::map<std::string, int> counts() const;
        [[nodiscard]] std::string lastError() const;
        [[nodiscard]] const std::filesystem::path& dbPath() const noexcept { return m_dbPath; }

        static constexpr int kMaxTentativi = 3;

      private:
        std::filesystem::path m_dbPath;
        mutable std::string m_lastError;
    };

} // namespace Core
