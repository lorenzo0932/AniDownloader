#pragma once
#include "Series.hpp"
#include "core/Database.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Core {
    class SeriesRepository {
      public:
        explicit SeriesRepository(const std::filesystem::path& jsonFilePath);
        // Shadow mode (B3, ADR-004 §5): il JSON resta autorevole per le
        // letture; ogni scrittura rispecchia il vettore anche nel DB e
        // verifica conteggi + hash. Le divergenze si osservano nei log e
        // nei contatori qui sotto, senza mai rompere il salvataggio JSON.
        // (jsonFilePath, dbPath) segue l'ordine "sorgente + destinazione"; lo scambio e' coperto
        // dai test. NOLINTNEXTLINE(bugprone-easily-swappable-parameters): vedi definizione in
        // SeriesRepository.cpp.
        SeriesRepository(const std::filesystem::path& jsonFilePath,
                         const std::filesystem::path& dbPath);
        const std::vector<Series>& loadSeriesData(bool forceReload = false);
        void saveSeriesData(const std::vector<Series>& seriesData);
        void invalidateCache();

        // Istantanea per valore copiata sotto lock: nessun riferimento alla
        // cache sopravvive al lock. Sostituisce l'uso di const& per i lettori.
        std::vector<Series> snapshotSeriesData(bool forceReload = false);

        // Mutazioni atomiche (una sola sezione critica load→verifica→save).
        // Il vincolo anti-duplicato vive qui, non nel controller HTTP.
        enum class MutazioneEsito { Ok, Duplicato, NonTrovato, ErroreStorage };
        struct AddEsito {
            MutazioneEsito esito = MutazioneEsito::Ok;
            std::size_t indice = 0;
            std::string identity;
            std::string errore;
        };
        struct UpdateEsito {
            MutazioneEsito esito = MutazioneEsito::Ok;
            std::string identity;
            std::string errore;
        };
        struct RemoveEsito {
            MutazioneEsito esito = MutazioneEsito::Ok;
            std::string errore;
        };
        AddEsito addSeries(const Series& serie);
        UpdateEsito updateSeries(std::size_t indice, const Series& serie);
        RemoveEsito removeSeries(std::size_t indice);

        // Aggiorna lastDownloadedEpisode/lastDownloadedAt per le serie in maxEpisodes
        // (una sola scrittura, dopo che tutti i download sono terminati).
        // Ritorna true se almeno una serie è stata aggiornata.
        bool applyDownloadedEpisodes(const std::map<std::string, int>& maxEpisodes,
                                     const std::string& timestamp);

        // Osservabilità shadow (test + diagnostica): quante scritture
        // rispecchiate, quante con divergenza, dettaglio dell'ultima.
        [[nodiscard]] bool shadowEnabled() const noexcept;
        [[nodiscard]] std::size_t shadowChecks() const;
        [[nodiscard]] std::size_t shadowDivergences() const;
        [[nodiscard]] std::string shadowLastInfo() const;

        // Path DB accanto al JSON (stesso nome, estensione .db). Path vuoto
        // o senza nome file → path vuoto = shadow disattivato.
        static std::filesystem::path dbPathFor(const std::filesystem::path& jsonFilePath);

      private:
        // Helper sotto lock (NON acquisiscono m_mutex: il chiamante lo detiene
        // già). loadLocked ricarica se necessario, saveLocked persiste e
        // aggiorna cache/stat/shadow, invalidateCacheLocked resetta la cache.
        const std::vector<Series>& loadLocked();
        std::string saveLocked(const std::vector<Series>& seriesData);
        void invalidateCacheLocked();
        // Apre il DB e importa dal JSON se la tabella series è vuota mentre
        // il JSON ha dati (primo avvio o import interrotto). Ritorna false
        // con log se l'import fallisce: il Repository continua in JSON-only.
        bool ensureShadowImported();
        // Rispecchia data nel DB (transazione) e verifica conteggi + hash.
        // Chiamare con m_mutex già acquisito, dopo il salvataggio JSON.
        bool writeShadow(const std::vector<Series>& data);
        // Cutover §6: lettura completa della tabella in ordine di rowid (= ordine
        // del JSON/_file_index). Riusata dal load e dalla verifica del mirror:
        // un solo mapping per entrambi i percorsi.
        static bool readAllFromDb(Database& db, std::vector<Series>& out);
        // Parità export su ricarica: hash del JSON su disco vs hash delle righe
        // lette dal DB. Divergenza = allarme in log (nessun auto-ripristino).
        void checkExportParity(const std::vector<Series>& dalDb);

        std::filesystem::path m_jsonFilePath;
        std::filesystem::path m_dbPath;
        std::optional<Database> m_db;
        std::optional<std::vector<Series>> m_cache; // Re-inserito optional
        mutable std::mutex m_mutex;
        // Snapshot del file al momento dell'ultimo load/save riuscito: se a
        // una load senza force lo stat differisce, il file è cambiato fuori
        // dal processo (es. altro daemon) e la cache va ricaricata.
        std::filesystem::file_time_type m_lastWrite{};
        std::uintmax_t m_lastSize{0};
        bool m_haveStat{false};
        bool m_shadowReady{false};
        std::size_t m_shadowChecks{0};
        std::size_t m_shadowDivergences{0};
        std::string m_shadowLastInfo{"shadow non attivo"};
    };
} // namespace Core