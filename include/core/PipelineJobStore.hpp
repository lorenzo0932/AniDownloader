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
//
// Dal modello Run (schema v6) ogni job appartiene a un run: `pipeline_runs`
// tiene una riga per avvio (manuale o automatico) con lo stato complessivo.
namespace Core {

    struct PipelineJob {
        std::int64_t id{0};
        std::string runId;
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

    // Run = un avvio della pipeline. I conteggi sono derivati dai job della
    // riga (unico dato autorevole: nessun contatore da tenere allineato),
    // tranne `saltati` che non ha un job corrispondente.
    struct PipelineRun {
        std::string id;
        std::string origine;    // background | interactive
        std::string avviatoIl;  // ISO8601 UTC
        std::string conclusoIl; // vuoto = ancora in corso (NULL in tabella)
        std::string stato;      // in_corso | completato | completato_con_errori | interrotto
        int saltati{0};
        int totale{0};
        int completati{0};
        int nonRiusciti{0}; // fase failed
        int interrotti{0};  // fase interrupted (non esposto dalle API: serve allo stato)
        int inCorso{0};     // job non terminali
        int percent{0};     // (completati + nonRiusciti) / totale * 100, arrotondato
    };

    // Conteggi per la vista "Avvii": calcolati sulla sola origine, non su
    // window/stato/limit (contratto /api/runs).
    struct RunCounts {
        int inCorso{0};
        int recenti{0};
        int problemi{0};
    };

    // Filtri della vista per-episodio (/api/pipeline). `fase = "problemi"`
    // è l'alias di failed + interrupted; `window` = recenti|storico|tutti.
    struct JobQuery {
        std::string fase;
        std::string runId;
        std::string window{"tutti"};
        int limit{100};
        int offset{0};
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
        // (origine + priorità alta) senza toccare la fase in corso. Se il job
        // esistente viene ripreso da un nuovo run, il suo `run_id` passa al
        // run corrente (il lavoro è di questo avvio).
        EsitoEnqueue enqueue(const std::string& jobKey, const std::string& serie, int episodio,
                             const std::string& origine, int priorita = 0,
                             const std::string& payload = "{}", const std::string& seriePath = "",
                             const std::string& runId = "");

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

        // ---- Run (schema v6) ----

        // Apre un run e ne ritorna l'id (`run-<epoch_ms>-<seq>`); vuoto su
        // errore di storage (i job restano con run_id vuoto, nessun crash).
        std::string beginRun(const std::string& origine);
        // Chiude il run con lo stato indicato; false se già chiuso o su errore.
        bool finishRun(const std::string& id, const std::string& stato);
        // Chiude come `interrotto` i run rimasti aperti (stop/crash del
        // processo precedente). Ritorna il numero chiuso, -1 su errore.
        int closeOrphanRuns();
        // Riepilogo con i conteggi derivati dai job; nullopt se il run non
        // esiste.
        [[nodiscard]] std::optional<PipelineRun> runSummary(const std::string& id) const;
        // Run più recente (avviato_il DESC): base di /api/overview.
        [[nodiscard]] std::vector<PipelineRun>
        listRuns(const std::string& window, const std::string& origine, const std::string& stato,
                 int limit, int offset, int* total = nullptr, RunCounts* counts = nullptr) const;
        [[nodiscard]] std::vector<PipelineJob> runJobs(const std::string& id,
                                                       int limit = 1000) const;
        // Rimette in coda i job `failed` del run (retryNow, tentativi intatti).
        // Ritorna il numero di job riprovati, -1 su errore.
        int retryFailedRun(const std::string& id);
        bool incrementSkipped(const std::string& id);

        [[nodiscard]] std::vector<PipelineJob> list(const std::string& fase = "",
                                                    int limit = 100) const;
        [[nodiscard]] std::vector<PipelineJob> list(const JobQuery& query) const;
        [[nodiscard]] std::map<std::string, int> counts() const;
        [[nodiscard]] std::string lastError() const;
        [[nodiscard]] const std::filesystem::path& dbPath() const noexcept { return m_dbPath; }

        static constexpr int kMaxTentativi = 3;

      private:
        std::filesystem::path m_dbPath;
        mutable std::string m_lastError;
    };

} // namespace Core
