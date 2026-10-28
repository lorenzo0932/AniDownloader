#include "core/SeriesRepository.hpp"
#include "core/DbImporter.hpp"
#include "core/Logger.hpp"
#include "core/SeriesIdentity.hpp"
#include <cstddef>
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

        // Il JSON esport contiene ancora dati? Serve per l'import di recupero:
        // DB vuoto mentre l'ex-autorevole ha contenuti. Mai lanciare: un parse
        // fallito equivale a "dati assenti" (il fallback gestisce il resto).
        bool jsonHasData(const std::filesystem::path& jsonPath) {
            try {
                std::ifstream f(jsonPath);
                if (!f.is_open())
                    return false;
                auto parsed = nlohmann::json::parse(f);
                return parsed.is_array() && !parsed.empty();
            } catch (const std::exception&) {
                return false;
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

        // Stat combinato dei file del DB (cutover §6). Il -wal si tocca a ogni
        // commit: senza contarlo, le scritture dello scheduler sulla stessa
        // tabella kv sembrerebbero invariate e la cache stagnerebbe.
        FileSnapshot snapshotDbFiles(const std::filesystem::path& dbPath) {
            FileSnapshot out;
            const auto db = snapshotFile(dbPath);
            const auto wal = snapshotFile(dbPath.string() + "-wal");
            if (db.valid) {
                out.mtime = db.mtime;
                out.size = db.size;
                out.valid = true;
            }
            if (wal.valid) {
                if (!out.valid || wal.mtime > out.mtime)
                    out.mtime = wal.mtime;
                out.size += wal.size;
                out.valid = true;
            }
            return out;
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

    // Lettura completa della tabella series: ordine di rowid (INSERT in ordine
    // array) mantiene la posizione indicizzata usata dall'API (_file_index).
    bool SeriesRepository::readAllFromDb(Database& db, std::vector<Series>& out) {
        out.clear();
        auto sel = db.prepare(
            "SELECT name, service, path, continue_series, is_high_priority, "
            "passed_episodes, series_page_url, episode_list_selector, "
            "download_link_selector, last_downloaded_at, last_downloaded_episode, "
            "alternate_sources FROM series ORDER BY rowid;");
        if (!sel.valid())
            return false;
        while (sel.step())
            out.push_back(DbImporter::readSeries(sel));
        return !sel.hasError();
    }

    // Parità export: l'export deve raccontare la stessa lista del DB. Su
    // divergenza soltanto allarme in log: decidere "quale copia vince"
    // automaticamente rischierebbe di cancellare contenuti più recenti; il
    // recupero passa da backup/restore (o dal binario precedente per l'export).
    void SeriesRepository::checkExportParity(const std::vector<Series>& dalDb) {
        if (dalDb.empty())
            return;
        std::vector<Series> esportato;
        try {
            std::ifstream f(m_jsonFilePath);
            if (!f.is_open())
                return; // export assente (prima scrittura): niente da confrontare
            auto arr = nlohmann::json::parse(f);
            esportato = arr.get<std::vector<Series>>();
        } catch (const std::exception& e) {
            Logger::error("Shadow DB: export JSON illeggibile: " + std::string(e.what()));
            return;
        }
        if (esportato.empty())
            return; // export assente (prima scrittura): niente da confrontare
        const std::string hashJson = DbImporter::seriesHash(esportato);
        const std::string hashDb = DbImporter::seriesHash(dalDb);
        // Sbagliato anche solo il conteggio è una divergenza: export vuoto con
        // DB pieno (o viceversa) deve generare l'allarme come contenuti diversi.
        if (esportato.size() == dalDb.size() && hashJson == hashDb)
            return;
        Logger::error("Shadow DB: DIVERGENZA export (" +
                      std::to_string(esportato.size()) + " serie, hash " + hashJson +
                      ") vs DB (" + std::to_string(dalDb.size()) + " serie, hash " +
                      hashDb + "): nessun auto-ripristino, recupero manuale da backup");
    }


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
        // Cutover: l'import di recupero parte solo con il DB vuoto mentre
        // l'export ha ancora contenuti, senza dipendere dalla cache in memoria
        // (al primo avvio la cache non esiste). È il caso "DB perso o
        // cancellato": si ricostruisce dal JSON senza mai riscriverlo.
        if (righe == 0 && jsonHasData(m_jsonFilePath)) {
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
                // Verifica dentro la transazione: conteggi + hash su rilettura
                // (stessa lettura usata dal cutover: un solo mapping).
                std::vector<Series> rilette;
                if (ok && !readAllFromDb(*m_db, rilette)) {
                    ok = false;
                    dettaglio = "verifica lettura: " + m_db->lastError();
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
        // Cutover: anche i successi entrano nei log come le divergenze, così
        // lo "zero-divergenze" è misurabile da /api/log durante la finestra
        // di rollback (i contatori non sono esposti via API).
        Logger::info("Shadow DB: " + m_shadowLastInfo);
        return true;
    }

    const std::vector<Series>& SeriesRepository::loadSeriesData(bool forceReload) {
        // Lettore legacy per contesti single-thread (test, CLI one-shot,
        // setup): il riferimento non sopravvive a mutazioni concorrenti.
        // Il codice runtime multithread usa snapshotSeriesData(). Delega a
        // loadLocked: un solo corpo di caricamento, nessuna duplicazione.
        std::lock_guard<std::mutex> lock(m_mutex);
        if (forceReload)
            invalidateCacheLocked();
        return loadLocked();
    }

    // --- Helper privati sotto lock (il chiamante detiene già m_mutex) ---

    // Ricarica la cache e la restituisce. NON acquisisce il mutex: chiamare
    // solo con m_mutex già acquisito.
    //
    // Cutover (ADR-004 §6): il DB è la fonte; il JSON è l'export e il fallback
    // di rollback (binario precedente = comportamento pre-cutover). Cache
    // valida finché i file del DB non cambiano; DB non apribile o lettura
    // fallita → si legge il file JSON esattamente come prima del cutover.
    const std::vector<Series>& SeriesRepository::loadLocked() {
        if (shadowEnabled()) {
            if (m_cache.has_value()) {
                if (!m_haveStat)
                    return m_cache.value();
                auto cur = snapshotDbFiles(m_dbPath);
                if (cur.valid && cur.mtime == m_lastWrite && cur.size == m_lastSize)
                    return m_cache.value();
            }
            if (ensureShadowImported()) {
                std::vector<Series> rilette;
                if (readAllFromDb(*m_db, rilette)) {
                    m_cache = std::move(rilette);
                    checkExportParity(m_cache.value());
                    if (auto s = snapshotDbFiles(m_dbPath); s.valid) {
                        m_lastWrite = s.mtime;
                        m_lastSize = s.size;
                        m_haveStat = true;
                    }
                    return m_cache.value();
                }
                Logger::error("Shadow DB: lettura fallita: " + m_db->lastError() +
                              " → fallback sul file JSON");
            }
        }

        if (!std::filesystem::exists(m_jsonFilePath)) {
            // error_code: un parent non creabile (es. file regolare) non deve
            // mai lanciare da qui; il salvataggio riporterà l'errore tipizzato.
            std::error_code ec;
            if (m_jsonFilePath.has_parent_path())
                std::filesystem::create_directories(m_jsonFilePath.parent_path(), ec);
            if (!ec) {
                std::ofstream outFile(m_jsonFilePath);
                if (outFile.is_open())
                    outFile << "[]";
            }
            if (!std::filesystem::exists(m_jsonFilePath)) {
                static const std::vector<Series> emptyFallback;
                return emptyFallback;
            }
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

    // Persiste data: DB prima (fonte, transazione + verifica), poi export
    // JSON. NON acquisisce il mutex: chiamare solo con m_mutex già acquisito.
    // Ritorna "" in caso di successo, altrimenti il messaggio d'errore.
    std::string SeriesRepository::saveLocked(const std::vector<Series>& seriesData) {
        try {
            if (shadowEnabled()) {
                // Un mirror fallito non blocca il salvataggio: il JSON esportato
                // resta la copia di rollback aggiornata e la divergenza è nei
                // log/contatori (ripristino manuale da backup). L'hash-parity
                // di ricarica segnalerà lo sfasamento finché non si recupera.
                if (!writeShadow(seriesData))
                    Logger::error("Shadow DB: mirror fallito: la scrittura risiede solo "
                                  "nell'export JSON (copia di rollback); il DB va "
                                  "verificato e, se serve, ripristinato da backup");
                try {
                    // Export atomico (tmp + rename): evita series_data.json
                    // corrotto su crash a metà scrittura.
                    nlohmann::json jsonArray = seriesData;
                    writeJsonAtomic(m_jsonFilePath, jsonArray, 4);
                } catch (const std::exception& e) {
                    // Il DB (fonte) ha comunque la scrittura: export/manutenzione
                    // danneggiata non deve sembrare una perdita dati.
                    Logger::error("Shadow DB: export JSON fallito (il DB conserva la "
                                  "scrittura): " + std::string(e.what()));
                }
                m_cache = seriesData; // Aggiorna la cache solo dopo il successo
                if (auto s = snapshotDbFiles(m_dbPath); s.valid) {
                    m_lastWrite = s.mtime;
                    m_lastSize = s.size;
                    m_haveStat = true;
                }
                return "";
            }

            // Legacy JSON-only (costruttore 1-arg: test/CLI one-shot senza DB):
            // comportamento pre-cutover invariato.
            nlohmann::json jsonArray = seriesData;
            writeJsonAtomic(m_jsonFilePath, jsonArray, 4);
            m_cache = seriesData;
            if (auto s = snapshotFile(m_jsonFilePath); s.valid) {
                m_lastWrite = s.mtime;
                m_lastSize = s.size;
                m_haveStat = true;
            }
            return "";
        } catch (const std::exception& e) {
            return e.what();
        }
    }

    std::vector<Series> SeriesRepository::snapshotSeriesData(bool forceReload) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (forceReload)
            invalidateCacheLocked();
        return loadLocked(); // copia sotto lock: il chiamante è al sicuro
    }

    SeriesRepository::AddEsito SeriesRepository::addSeries(const Series& serie) {
        std::lock_guard<std::mutex> lock(m_mutex);
        AddEsito out;
        try {
            out.identity = seriesIdentity(serie.service, serie.seriesPageUrl);
            auto current = loadLocked();
            for (size_t i = 0; i < current.size(); ++i) {
                if (!out.identity.empty() &&
                    seriesIdentity(current[i].service, current[i].seriesPageUrl) == out.identity) {
                    out.esito = MutazioneEsito::Duplicato;
                    out.indice = i;
                    return out;
                }
            }
            current.push_back(serie);
            if (std::string err = saveLocked(current); !err.empty()) {
                out.esito = MutazioneEsito::ErroreStorage;
                out.errore = err;
                return out;
            }
            out.esito = MutazioneEsito::Ok;
            out.indice = current.size() - 1;
            return out;
        } catch (const std::exception& e) {
            out.esito = MutazioneEsito::ErroreStorage;
            out.errore = e.what();
            return out;
        }
    }

    SeriesRepository::UpdateEsito SeriesRepository::updateSeries(std::size_t indice,
                                                                const Series& serie) {
        std::lock_guard<std::mutex> lock(m_mutex);
        UpdateEsito out;
        try {
            out.identity = seriesIdentity(serie.service, serie.seriesPageUrl);
            auto current = loadLocked();
            if (indice >= current.size()) {
                out.esito = MutazioneEsito::NonTrovato;
                return out;
            }
            for (size_t i = 0; i < current.size(); ++i) {
                if (i == indice)
                    continue; // l'identità invariata della serie stessa è ammessa
                if (!out.identity.empty() &&
                    seriesIdentity(current[i].service, current[i].seriesPageUrl) == out.identity) {
                    out.esito = MutazioneEsito::Duplicato;
                    return out;
                }
            }
            current[indice] = serie;
            if (std::string err = saveLocked(current); !err.empty()) {
                out.esito = MutazioneEsito::ErroreStorage;
                out.errore = err;
                return out;
            }
            out.esito = MutazioneEsito::Ok;
            return out;
        } catch (const std::exception& e) {
            out.esito = MutazioneEsito::ErroreStorage;
            out.errore = e.what();
            return out;
        }
    }

    SeriesRepository::RemoveEsito SeriesRepository::removeSeries(std::size_t indice) {
        std::lock_guard<std::mutex> lock(m_mutex);
        RemoveEsito out;
        try {
            auto current = loadLocked();
            if (indice >= current.size()) {
                out.esito = MutazioneEsito::NonTrovato;
                return out;
            }
            current.erase(current.begin() + static_cast<std::ptrdiff_t>(indice));
            if (std::string err = saveLocked(current); !err.empty()) {
                out.esito = MutazioneEsito::ErroreStorage;
                out.errore = err;
                return out;
            }
            out.esito = MutazioneEsito::Ok;
            return out;
        } catch (const std::exception& e) {
            out.esito = MutazioneEsito::ErroreStorage;
            out.errore = e.what();
            return out;
        }
    }

    void SeriesRepository::saveSeriesData(const std::vector<Series>& seriesData) {
        // Percorso legacy per import/setup/test single-thread. Il codice
        // runtime multithread usa add/update/remove (vincolo anti-duplicato).
        std::lock_guard<std::mutex> lock(m_mutex);
        if (std::string err = saveLocked(seriesData); !err.empty())
            Logger::error("Errore salvataggio dati: " + err);
    }

    void SeriesRepository::invalidateCacheLocked() {
        m_cache.reset();
        m_haveStat = false;
    }

    void SeriesRepository::invalidateCache() {
        std::lock_guard<std::mutex> lock(m_mutex);
        invalidateCacheLocked();
    }

    bool SeriesRepository::applyDownloadedEpisodes(const std::map<std::string, int>& maxEpisodes,
                                                   const std::string& timestamp) {
        if (maxEpisodes.empty())
            return false;

        // Atomico: lettura, avanzamento e scrittura nella stessa sezione
        // critica, sull'ultima lista disponibile (mai snapshot vecchi).
        std::lock_guard<std::mutex> lock(m_mutex);
        auto series = loadLocked();

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
            if (std::string err = saveLocked(series); !err.empty()) {
                Logger::error("Errore salvataggio avanzamento: " + err);
                return false;
            }
        }
        return updated;
    }
} // namespace Core