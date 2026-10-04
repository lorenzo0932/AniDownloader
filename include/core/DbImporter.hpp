#pragma once

#include "core/Database.hpp"
#include "core/Series.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

// Importatore JSON → SQLite (B2, ADR-004 §3): backup prima di tutto,
// dry-run, import in transazione, verifica conteggi + hash per campo,
// rollback automatico su qualunque errore. L'originale non viene mai
// toccato senza backup verificato.
namespace Core {

    struct ImportStats {
        std::size_t lette{0};
        std::size_t importate{0};
        std::string hashJson;
        std::string hashDb;
    };

    struct ImportResult {
        bool ok{false};
        ImportStats stats;
        std::string error;
        std::filesystem::path backupPath;
    };

    class DbImporter {
      public:
        // jsonPath: series_data.json esistente. dbPath: database destinazione
        // (creato se manca). dryRun: valida senza scrivere nulla.
        // (jsonPath, dbPath) segue l'ordine "sorgente + destinazione"; lo scambio e' coperto dai
        // test. NOLINTNEXTLINE(bugprone-easily-swappable-parameters): vedi definizione in
        // DbImporter.cpp.
        static ImportResult importJson(const std::filesystem::path& jsonPath,
                                       const std::filesystem::path& dbPath, bool dryRun = false);

        // Hash FNV-1a su dump canonico (to_json ha ordine campi fisso):
        // uguale hash su JSON e su righe rilette = import fedele.
        static std::string seriesHash(const std::vector<Series>& series);

        // Mapping riga <-> Series, riusato da B3 (cutover) per non
        // duplicare la corrispondenza colonne/campi in due punti.
        static bool bindSeries(Statement& stmt, const Series& s);
        static Series readSeries(Statement& stmt);

      private:
        DbImporter() = delete;
    };

} // namespace Core
