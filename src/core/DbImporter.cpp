#include "core/DbImporter.hpp"

#include "core/Logger.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <fstream>
#include <sstream>

namespace Core {

    namespace {
        // FNV-1a 64 bit: basta per confronto integrità, non serve crypto.
        constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
        constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

        std::uint64_t fnv1a(std::uint64_t hash, const std::string& data) {
            for (unsigned char c : data) {
                hash ^= c;
                hash *= kFnvPrime;
            }
            return hash;
        }

        std::string toHex(std::uint64_t value) {
            std::ostringstream out;
            out << std::hex << value;
            return out.str();
        }

        // Timestamp locale compatto per il nome backup: 20261004T183000.
        std::string backupTimestamp() {
            const auto now = std::chrono::system_clock::now();
            return std::format("{:%Y%m%dT%H%M%S}", std::chrono::floor<std::chrono::seconds>(now));
        }

        bool sameSize(const std::filesystem::path& a, const std::filesystem::path& b) {
            std::error_code ec;
            const auto sa = std::filesystem::file_size(a, ec);
            if (ec)
                return false;
            const auto sb = std::filesystem::file_size(b, ec);
            return !ec && sa == sb;
        }
    } // namespace

    std::string DbImporter::seriesHash(const std::vector<Series>& series) {
        // Ordinamento per nome: il JSON ha l'ordine d'inserzione, il DB
        // rilegge con ORDER BY name — l'hash deve ignorare l'ordine.
        std::vector<const Series*> ordinate;
        ordinate.reserve(series.size());
        for (const auto& s : series)
            ordinate.push_back(&s);
        std::sort(ordinate.begin(), ordinate.end(),
                  [](const Series* a, const Series* b) { return a->name < b->name; });
        std::uint64_t hash = kFnvOffset;
        for (const Series* s : ordinate)
            hash = fnv1a(hash, nlohmann::json(*s).dump());
        return toHex(hash);
    }

    bool DbImporter::bindSeries(Statement& stmt, const Series& s) {
        nlohmann::json alt = nlohmann::json::array();
        for (const auto& a : s.alternateSources)
            alt.push_back({{"service", a.service}, {"series_page_url", a.seriesPageUrl}});
        return stmt.bindText(1, s.name) && stmt.bindText(2, s.service) &&
               stmt.bindText(3, s.path) && stmt.bindInt(4, s.continueSeries ? 1 : 0) &&
               stmt.bindInt(5, s.isHighPriority ? 1 : 0) && stmt.bindInt(6, s.passedEpisodes) &&
               stmt.bindText(7, s.seriesPageUrl) && stmt.bindText(8, s.episodeListSelector) &&
               stmt.bindText(9, s.downloadLinkSelector) && stmt.bindText(10, s.lastDownloadedAt) &&
               stmt.bindInt(11, s.lastDownloadedEpisode) && stmt.bindText(12, alt.dump());
    }

    Series DbImporter::readSeries(Statement& stmt) {
        Series s;
        s.name = stmt.columnText(0);
        s.service = stmt.columnText(1);
        s.path = stmt.columnText(2);
        s.continueSeries = stmt.columnInt(3) != 0;
        s.isHighPriority = stmt.columnInt(4) != 0;
        s.passedEpisodes = static_cast<int>(stmt.columnInt(5));
        s.seriesPageUrl = stmt.columnText(6);
        s.episodeListSelector = stmt.columnText(7);
        s.downloadLinkSelector = stmt.columnText(8);
        s.lastDownloadedAt = stmt.columnText(9);
        s.lastDownloadedEpisode = static_cast<int>(stmt.columnInt(10));
        try {
            for (const auto& item : nlohmann::json::parse(stmt.columnText(11))) {
                AlternateSource a;
                a.service = item.value("service", "");
                a.seriesPageUrl = item.value("series_page_url", "");
                if (!a.service.empty() && !a.seriesPageUrl.empty())
                    s.alternateSources.push_back(a);
            }
        } catch (const std::exception& e) {
            Logger::error("alternate_sources non valido per '" + s.name + "': " + e.what());
        }
        return s;
    }

    namespace {
        constexpr const char* kInsertSeries = R"(
INSERT OR REPLACE INTO series(name, service, path, continue_series, is_high_priority,
passed_episodes, series_page_url, episode_list_selector, download_link_selector,
last_downloaded_at, last_downloaded_episode, alternate_sources)
VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);)";

        constexpr const char* kSelectAllSeries = R"(
SELECT name, service, path, continue_series, is_high_priority, passed_episodes,
series_page_url, episode_list_selector, download_link_selector,
last_downloaded_at, last_downloaded_episode, alternate_sources
FROM series ORDER BY name;)";
    } // namespace

    // (jsonPath, dbPath) segue l'ordine "sorgente + destinazione"; lo scambio e' coperto dai test.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    ImportResult DbImporter::importJson(const std::filesystem::path& jsonPath,
                                        const std::filesystem::path& dbPath, bool dryRun) {
        ImportResult result;
        if (!std::filesystem::exists(jsonPath)) {
            result.error = "file JSON non trovato: " + jsonPath.string();
            return result;
        }

        // 1. Parse con lo stesso codice del Repository: ciò che il JSON
        // significa per l'app è ciò che viene importato.
        std::vector<Series> series;
        try {
            std::ifstream in(jsonPath);
            nlohmann::json jsonArray;
            in >> jsonArray;
            series = jsonArray.get<std::vector<Series>>();
        } catch (const std::exception& e) {
            result.error = "JSON malformato: " + std::string(e.what());
            return result;
        }
        result.stats.lette = series.size();
        result.stats.hashJson = seriesHash(series);
        if (dryRun) {
            result.ok = true;
            return result;
        }

        // 2. Backup prima di qualunque scrittura + verifica byte-identico.
        // Mai sovrascrivere un backup esistente (stesso secondo, retry
        // rapidi): suffisso progressivo.
        result.backupPath = jsonPath;
        result.backupPath += ".bak." + backupTimestamp();
        for (int tentativo = 2; std::filesystem::exists(result.backupPath); ++tentativo)
            result.backupPath = std::filesystem::path(
                jsonPath.string() + ".bak." + backupTimestamp() + "." + std::to_string(tentativo));
        std::error_code ec;
        std::filesystem::copy_file(jsonPath, result.backupPath, ec);
        if (ec || !sameSize(jsonPath, result.backupPath)) {
            result.error = "backup fallito: " + ec.message();
            result.backupPath.clear();
            return result;
        }

        // 3. Import in transazione: qualunque errore = rollback automatico.
        Database db(dbPath);
        if (!db.isOpen()) {
            result.error = "apertura DB: " + db.lastError();
            return result;
        }
        Transaction tx(db);
        if (!tx.active()) {
            result.error = "BEGIN: " + db.lastError();
            return result;
        }
        auto insert = db.prepare(kInsertSeries);
        if (!insert.valid()) {
            result.error = "prepare: " + insert.lastError();
            return result;
        }
        for (const auto& s : series) {
            if (!DbImporter::bindSeries(insert, s) || (insert.step(), insert.hasError())) {
                result.error = "insert '" + s.name + "': " + insert.lastError();
                return result;
            }
            if (!insert.reset()) {
                result.error = "reset: " + insert.lastError();
                return result;
            }
        }

        // 4. Verifica: rilettura totale + conteggi + hash per campo.
        // La verifica avviene DENTRO la transazione: se fallisce, il
        // rollback cancella anche un import parzialmente giusto.
        std::vector<Series> rilette;
        {
            auto sel = db.prepare(kSelectAllSeries);
            if (!sel.valid()) {
                result.error = "select verifica: " + sel.lastError();
                return result;
            }
            while (sel.step())
                rilette.push_back(DbImporter::readSeries(sel));
            if (sel.hasError()) {
                result.error = "lettura verifica: " + sel.lastError();
                return result;
            }
        }
        result.stats.importate = rilette.size();
        result.stats.hashDb = seriesHash(rilette);
        if (rilette.size() != series.size() || result.stats.hashDb != result.stats.hashJson) {
            result.error = "verifica fallita: lette " + std::to_string(series.size()) +
                           " rilette " + std::to_string(rilette.size()) + " hash " +
                           result.stats.hashJson + " vs " + result.stats.hashDb;
            return result;
        }
        if (!tx.commit()) {
            result.error = "COMMIT: " + db.lastError();
            return result;
        }
        result.ok = true;
        return result;
    }

} // namespace Core
