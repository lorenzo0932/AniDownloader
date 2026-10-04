#include "core/SeriesRepository.hpp"
#include "core/DbImporter.hpp"
#include "core/Logger.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace Core {

    namespace {

        // Scrittura JSON atomica (tmp nella stessa dir + rename; fallback copy su
        // Windows dove rename fallisce con target esistente). Su eccezione rimuove il
        // tmp: il chiamante decide se loggare.
        void writeJsonAtomic(const std::filesystem::path& target, const nlohmann::json& data,
                             int indent) {
            if (target.has_parent_path()) {
                std::filesystem::create_directories(target.parent_path());
            }
            auto tmpPath = target;
            tmpPath += ".tmp";
            {
                std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
                if (!f.is_open())
                    throw std::runtime_error("Impossibile aprire " + tmpPath.string());
                f << data.dump(indent);
            }
            std::error_code ec;
            std::filesystem::rename(tmpPath, target, ec);
            if (ec) {
                std::filesystem::copy_file(tmpPath, target,
                                           std::filesystem::copy_options::overwrite_existing, ec);
                if (ec)
                    throw std::runtime_error("copy fallito: " + ec.message());
                std::filesystem::remove(tmpPath, ec);
            }
        }

    } // namespace

    namespace {

        struct FileSnapshot {
            std::filesystem::file_time_type mtime;
            std::uintmax_t size{0};
            bool valid{false};
        };

        // Stat con error_code: mai eccezioni (file cancellato in corsa -> invalid).
        FileSnapshot snapshotFile(const std::filesystem::path& p) {
            FileSnapshot s;
            std::error_code ec;
            auto t = std::filesystem::last_write_time(p, ec);
            if (ec)
                return s;
            auto sz = std::filesystem::file_size(p, ec);
            if (ec)
                return s;
            s.mtime = t;
            s.size = sz;
            s.valid = true;
            return s;
        }

    } // namespace

    SeriesRepository::SeriesRepository(const std::filesystem::path& jsonFilePath)
        : m_jsonFilePath(jsonFilePath) {}

    // (jsonFilePath, dbPath) segue l'ordine "sorgente + destinazione"; lo scambio e' coperto dai
    // test. NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    SeriesRepository::SeriesRepository(const std::filesystem::path& jsonFilePath,
                                       const std::filesystem::path& dbPath)
        : m_jsonFilePath(jsonFilePath), m_dbPath(dbPath) {
        m_shadowLastInfo = "shadow configurato, in attesa della prima scrittura";
    }

    std::filesystem::path SeriesRepository::dbPathFor(const std::filesystem::path& jsonFilePath) {
        if (jsonFilePath.empty() || !jsonFilePath.has_filename())
            return {};
        auto dbPath = jsonFilePath;
        return dbPath.replace_extension(".db");
    }

    bool SeriesRepository::shadowEnabled() const noexcept { return !m_dbPath.empty(); }

    std::size_t SeriesRepository::shadowChecks() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_shadowChecks;
    }

    std::size_t SeriesRepository::shadowDivergences() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_shadowDivergences;
    }

    std::string SeriesRepository::shadowLastInfo() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_shadowLastInfo;
    }

    bool SeriesRepository::ensureShadowImported() {
        if (m_shadowReady)
            return true;
        m_db.emplace(m_dbPath);
        if (!m_db->isOpen()) {
            m_shadowLastInfo = "apertura DB: " + m_db->lastError();
            Logger::error("Shadow DB: " + m_shadowLastInfo);
            m_db.reset();
            return false;
        }
        // Conta le righe: DB vuoto + JSON popolato = primo avvio.
        std::size_t righe = 0;
        {
            auto count = m_db->prepare("SELECT COUNT(*) FROM series;");
            if (count.valid() && count.step() && !count.hasError())
                righe = static_cast<std::size_t>(count.columnInt(0));
        }
        if (righe == 0 && m_cache.has_value() && !m_cache->empty()) {
            auto res = DbImporter::importJson(m_jsonFilePath, m_dbPath);
            if (!res.ok) {
                m_shadowLastInfo = "import iniziale: " + res.error;
                Logger::error("Shadow DB: " + m_shadowLastInfo);
                return false;
            }
            Logger::info("Shadow DB: importate " + std::to_string(res.stats.importate) +
                         " serie (backup " + res.backupPath.filename().string() + ")");
        }
        m_shadowReady = true;
        return true;
    }

    bool SeriesRepository::writeShadow(const std::vector<Series>& data) {
        // Ogni scrittura è una verifica: qualunque fallimento del mirror
        // (DB illeggibile, tabella mancante, hash diverso) è una divergenza
        // tra JSON e DB e va contata come tale.
        ++m_shadowChecks;
        bool ok = ensureShadowImported();
        std::string dettaglio;
        if (!ok) {
            dettaglio = m_shadowLastInfo;
        } else if (!m_db.has_value()) {
            // Difensivo: ensureShadowImported riuscito implica DB aperto.
            ok = false;
            dettaglio = "DB non aperto dopo import";
        } else {
            // Transazione unica mirror + verifica: o tutto o niente.
            Transaction tx(*m_db);
            if (!tx.active()) {
                ok = false;
                dettaglio = "BEGIN: " + m_db->lastError();
            } else {
                std::string error;
                if (!m_db->execute("DELETE FROM series;", &error)) {
                    ok = false;
                    dettaglio = "mirror DELETE: " + error;
                } else {
                    auto insert = m_db->prepare(
                        "INSERT INTO series(name, service, path, continue_series, "
                        "is_high_priority, "
                        "passed_episodes, series_page_url, episode_list_selector, "
                        "download_link_selector, last_downloaded_at, last_downloaded_episode, "
                        "alternate_sources) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
                    if (!insert.valid()) {
                        ok = false;
                        dettaglio = "mirror prepare: " + insert.lastError();
                    } else {
                        for (const auto& s : data) {
                            if (!DbImporter::bindSeries(insert, s) ||
                                (insert.step(), insert.hasError()) || !insert.reset()) {
                                ok = false;
                                dettaglio = "mirror insert '" + s.name + "': " + insert.lastError();
                                break;
                            }
                        }
                    }
                }
                // Verifica dentro la transazione: conteggi + hash su rilettura.
                std::vector<Series> rilette;
                if (ok) {
                    auto sel = m_db->prepare(
                        "SELECT name, service, path, continue_series, is_high_priority, "
                        "passed_episodes, "
                        "series_page_url, episode_list_selector, download_link_selector, "
                        "last_downloaded_at, last_downloaded_episode, alternate_sources FROM "
                        "series;");
                    if (!sel.valid()) {
                        ok = false;
                        dettaglio = "verifica prepare: " + sel.lastError();
                    } else {
                        while (sel.step())
                            rilette.push_back(DbImporter::readSeries(sel));
                        if (sel.hasError()) {
                            ok = false;
                            dettaglio = "verifica lettura: " + sel.lastError();
                        }
                    }
                }
                if (ok) {
                    const std::string hashJson = DbImporter::seriesHash(data);
                    const std::string hashDb = DbImporter::seriesHash(rilette);
                    if (rilette.size() != data.size() || hashDb != hashJson) {
                        ok = false;
                        dettaglio = "DIVERGENZA n=" + std::to_string(data.size()) + " vs " +
                                    std::to_string(rilette.size()) + " hash " + hashJson + " vs " +
                                    hashDb;
                    } else if (!tx.commit()) {
                        ok = false;
                        dettaglio = "COMMIT: " + m_db->lastError();
                    } else {
                        dettaglio = "ok n=" + std::to_string(data.size()) + " hash " + hashDb;
                    }
                }
            }
        }
        if (!ok) {
            ++m_shadowDivergences;
            m_shadowLastInfo = dettaglio;
            Logger::error("Shadow DB: " + m_shadowLastInfo);
            return false;
        }
        m_shadowLastInfo = dettaglio + " (verifiche " + std::to_string(m_shadowChecks) +
                           ", divergenze " + std::to_string(m_shadowDivergences) + ")";
        return true;
    }

    const std::vector<Series>& SeriesRepository::loadSeriesData(bool forceReload) {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!forceReload && m_cache.has_value() && m_haveStat) {
            auto cur = snapshotFile(m_jsonFilePath);
            if (cur.valid && cur.mtime == m_lastWrite && cur.size == m_lastSize) {
                return m_cache.value(); // file invariato: cache valida
            }
            // mismatch (o stat fallito): ricade nel reload qui sotto
        } else if (!forceReload && m_cache.has_value()) {
            return m_cache.value();
        }

        if (!std::filesystem::exists(m_jsonFilePath)) {
            if (m_jsonFilePath.has_parent_path()) {
                std::filesystem::create_directories(m_jsonFilePath.parent_path());
            }
            std::ofstream outFile(m_jsonFilePath);
            outFile << "[]";
            m_cache = std::vector<Series>{};
            if (auto s = snapshotFile(m_jsonFilePath); s.valid) {
                m_lastWrite = s.mtime;
                m_lastSize = s.size;
                m_haveStat = true;
            }
            return m_cache.value();
        }

        try {
            std::ifstream inFile(m_jsonFilePath);
            nlohmann::json jsonArray;
            inFile >> jsonArray;
            m_cache = jsonArray.get<std::vector<Series>>();
        } catch (const std::exception& e) {
            Logger::error("Errore caricamento dati: " + std::string(e.what()));
            static const std::vector<Series> emptyFallback;
            return emptyFallback;
        }
        if (auto s = snapshotFile(m_jsonFilePath); s.valid) {
            m_lastWrite = s.mtime;
            m_lastSize = s.size;
            m_haveStat = true;
        }
        return m_cache.value();
    }

    void SeriesRepository::saveSeriesData(const std::vector<Series>& seriesData) {
        std::lock_guard<std::mutex> lock(m_mutex);

        try {
            nlohmann::json jsonArray = seriesData;
            // Scrittura atomica (tmp + rename): evita series_data.json corrotto
            // su crash a metà scrittura.
            writeJsonAtomic(m_jsonFilePath, jsonArray, 4);
            m_cache = seriesData; // Aggiorna la cache solo dopo il successo
            if (auto s = snapshotFile(m_jsonFilePath); s.valid) {
                m_lastWrite = s.mtime;
                m_lastSize = s.size;
                m_haveStat = true;
            }
            // Shadow dopo il successo JSON: un suo fallimento non tocca mai
            // il salvataggio appena riuscito (log + contatori, niente throw).
            if (shadowEnabled())
                writeShadow(seriesData);
        } catch (const std::exception& e) {
            Logger::error("Errore salvataggio dati: " + std::string(e.what()));
        }
    }

    void SeriesRepository::invalidateCache() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cache.reset();
        m_haveStat = false;
    }

    bool SeriesRepository::applyDownloadedEpisodes(const std::map<std::string, int>& maxEpisodes,
                                                   const std::string& timestamp) {
        if (maxEpisodes.empty())
            return false;

        // loadSeriesData gestisce il lock: unico punto di accesso alla cache.
        // Copia necessaria: la cache è esposta come const&.
        auto series = loadSeriesData();

        bool updated = false;
        for (auto& s : series) {
            auto it = maxEpisodes.find(s.name);
            if (it == maxEpisodes.end())
                continue;
            // Semantica: episodio e timestamp si aggiornano SOLO con avanzamento
            // reale. Conversioni locali di manutenzione o episodi non più alti
            // non devono "sporcare" il timestamp né scrivere il file.
            if (it->second > s.lastDownloadedEpisode) {
                s.lastDownloadedEpisode = it->second;
                s.lastDownloadedAt = timestamp;
                updated = true;
            }
        }

        if (updated) {
            saveSeriesData(series);
        }
        return updated;
    }
} // namespace Core