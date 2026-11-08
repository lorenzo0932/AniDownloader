#include "core/PipelineJobStore.hpp"

#include "core/Database.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <utility>

namespace Core {

    namespace {
        std::int64_t oraEpoch() {
            return std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }

        // Cutoff della finestra "recenti" (24h): stessa espressione nel WHERE
        // e nei conteggi, così nessun clock lato C++ può divergere.
        constexpr const char* kCutoff24h = "strftime('%Y-%m-%dT%H:%M:%fZ','now','-24 hours')";

        constexpr const char* kColonne =
            "id, run_id, job_key, serie, episodio, origine, fase, tentativi, "
            "prossima_esecuzione, priorita, errore, payload, serie_path, file_path, creato_il, "
            "aggiornato_il";

        PipelineJob leggiRiga(Statement& sel) {
            PipelineJob j;
            j.id = sel.columnInt(0);
            j.runId = sel.columnText(1);
            j.jobKey = sel.columnText(2);
            j.serie = sel.columnText(3);
            j.episodio = static_cast<int>(sel.columnInt(4));
            j.origine = sel.columnText(5);
            j.fase = sel.columnText(6);
            j.tentativi = static_cast<int>(sel.columnInt(7));
            j.prossimaEsecuzione = sel.columnInt(8);
            j.priorita = static_cast<int>(sel.columnInt(9));
            j.errore = sel.columnText(10);
            j.payload = sel.columnText(11);
            j.seriePath = sel.columnText(12);
            j.filePath = sel.columnText(13);
            j.creatoIl = sel.columnText(14);
            j.aggiornatoIl = sel.columnText(15);
            return j;
        }

        std::optional<PipelineJob> leggiPerId(Database& db, std::int64_t id) {
            auto sel =
                db.prepare(std::string("SELECT ") + kColonne + " FROM pipeline_jobs WHERE id = ?;");
            if (!sel.valid() || !sel.bindInt(1, id) || !sel.step() || sel.hasError())
                return std::nullopt;
            return leggiRiga(sel);
        }

        int percentuale(int completati, int nonRiusciti, int totale) {
            if (totale <= 0)
                return 0;
            const int fatti = completati + nonRiusciti;
            // Arrotondamento all'intero più vicino senza virgola mobile.
            return (fatti * 100 + totale / 2) / totale;
        }

        // Colonne condivise da runSummary e listRuns: conteggi derivati dai
        // job del run (LEFT JOIN: un run appena aperto ha zero job).
        constexpr const char* kColonneRun =
            "r.id, r.origine, r.avviato_il, r.concluso_il, r.stato, r.saltati, "
            "COUNT(j.id), "
            "COALESCE(SUM(CASE WHEN j.fase = 'completed' THEN 1 ELSE 0 END), 0), "
            "COALESCE(SUM(CASE WHEN j.fase = 'failed' THEN 1 ELSE 0 END), 0), "
            "COALESCE(SUM(CASE WHEN j.fase = 'interrupted' THEN 1 ELSE 0 END), 0), "
            "COALESCE(SUM(CASE WHEN j.fase IN ('queued_download', 'downloading', "
            "'downloaded_waiting_conversion', 'converting', 'retry_wait') "
            "THEN 1 ELSE 0 END), 0)";

        PipelineRun leggiRun(Statement& sel) {
            PipelineRun r;
            r.id = sel.columnText(0);
            r.origine = sel.columnText(1);
            r.avviatoIl = sel.columnText(2);
            r.conclusoIl = sel.columnText(3);
            r.stato = sel.columnText(4);
            r.saltati = static_cast<int>(sel.columnInt(5));
            r.totale = static_cast<int>(sel.columnInt(6));
            r.completati = static_cast<int>(sel.columnInt(7));
            r.nonRiusciti = static_cast<int>(sel.columnInt(8));
            r.interrotti = static_cast<int>(sel.columnInt(9));
            r.inCorso = static_cast<int>(sel.columnInt(10));
            r.percent = percentuale(r.completati, r.nonRiusciti, r.totale);
            return r;
        }

        // "manuale" e "automatico" sono i nomi pubblici delle origini
        // interne; "tutte" (o vuoto) non filtra.
        std::string normalizzaOrigine(const std::string& origine) {
            if (origine == "manuale" || origine == "interactive")
                return "interactive";
            if (origine == "automatico" || origine == "background")
                return "background";
            return {};
        }

        std::string statoRunValido(const std::string& stato) {
            if (stato == "in_corso" || stato == "completato" || stato == "completato_con_errori" ||
                stato == "interrotto")
                return stato;
            return {};
        }
    } // namespace

    PipelineJobStore::PipelineJobStore(std::filesystem::path dbPath)
        : m_dbPath(std::move(dbPath)) {}

    std::string PipelineJobStore::lastError() const { return m_lastError; }

    PipelineJobStore::EsitoEnqueue
    PipelineJobStore::enqueue(const std::string& jobKey, const std::string& serie, int episodio,
                              const std::string& origine, int priorita, const std::string& payload,
                              const std::string& seriePath, const std::string& runId) {
        EsitoEnqueue esito;
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return esito;
        }
        Transaction tx(db);
        if (!tx.active()) {
            m_lastError = db.lastError();
            return esito;
        }
        const bool interattivo = origine == "interactive";
        auto sel = db.prepare("SELECT id, origine FROM pipeline_jobs WHERE job_key = ? "
                              "AND fase NOT IN ('completed', 'failed') LIMIT 1;");
        if (!sel.valid() || !sel.bindText(1, jobKey)) {
            m_lastError = sel.lastError();
            return esito;
        }
        if (sel.step()) {
            esito.id = sel.columnInt(0);
            const std::string origineEsistente = sel.columnText(1);
            sel.reset();
            const bool promuove = interattivo && origineEsistente != "interactive";
            if (promuove || !runId.empty()) {
                // Il job ripreso da un nuovo run passa al run corrente: il
                // lavoro è di questo avvio (es. job rimasto in coda dopo uno
                // stop e riplanificato).
                auto upd =
                    db.prepare("UPDATE pipeline_jobs SET "
                               "origine = CASE WHEN ? THEN 'interactive' ELSE origine END, "
                               "priorita = CASE WHEN ? THEN MAX(priorita, ?) ELSE priorita END, "
                               "run_id = CASE WHEN ? = '' THEN run_id ELSE ? END, "
                               "aggiornato_il = strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                               "WHERE id = ?;");
                if (!upd.valid() || !upd.bindInt(1, promuove ? 1 : 0) ||
                    !upd.bindInt(2, promuove ? 1 : 0) ||
                    !upd.bindInt(3, (std::max)(priorita, 100)) || !upd.bindText(4, runId) ||
                    !upd.bindText(5, runId) || !upd.bindInt(6, esito.id) ||
                    (upd.step(), upd.hasError())) {
                    m_lastError = upd.lastError();
                    return esito;
                }
                esito.promosso = promuove || (interattivo && origineEsistente == "interactive");
            } else if (interattivo) {
                esito.promosso = true;
            }
            if (!tx.commit()) {
                m_lastError = db.lastError();
                return esito;
            }
            return esito;
        }
        auto ins = db.prepare("INSERT INTO pipeline_jobs(job_key, serie, episodio, origine, "
                              "priorita, payload, serie_path, run_id) "
                              "VALUES (?, ?, ?, ?, ?, ?, ?, ?);");
        if (!ins.valid() || !ins.bindText(1, jobKey) || !ins.bindText(2, serie) ||
            !ins.bindInt(3, episodio) || !ins.bindText(4, origine) || !ins.bindInt(5, priorita) ||
            !ins.bindText(6, payload) || !ins.bindText(7, seriePath) || !ins.bindText(8, runId) ||
            (ins.step(), ins.hasError())) {
            m_lastError = ins.lastError();
            return esito;
        }
        auto id = db.prepare("SELECT last_insert_rowid();");
        if (!id.valid() || !id.step() || id.hasError()) {
            m_lastError = id.lastError();
            return esito;
        }
        esito.id = id.columnInt(0);
        esito.creato = true;
        if (!tx.commit()) {
            m_lastError = db.lastError();
            return esito;
        }
        return esito;
    }

    std::optional<PipelineJob> PipelineJobStore::claim(const std::string& daFase,
                                                       const std::string& aFase, std::int64_t ora) {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return std::nullopt;
        }
        const auto adesso = ora > 0 ? ora : oraEpoch();
        Transaction tx(db);
        if (!tx.active()) {
            m_lastError = db.lastError();
            return std::nullopt;
        }
        std::int64_t id = 0;
        {
            auto sel = db.prepare("SELECT id FROM pipeline_jobs WHERE fase = ? "
                                  "AND prossima_esecuzione <= ? "
                                  "ORDER BY priorita DESC, id LIMIT 1;");
            if (!sel.valid() || !sel.bindText(1, daFase) || !sel.bindInt(2, adesso) ||
                !sel.step() || sel.hasError()) {
                m_lastError = sel.lastError();
                return std::nullopt;
            }
            id = sel.columnInt(0);
        }
        auto upd = db.prepare("UPDATE pipeline_jobs SET fase = ?, errore = '', "
                              "aggiornato_il = strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                              "WHERE id = ? AND fase = ?;");
        if (!upd.valid() || !upd.bindText(1, aFase) || !upd.bindInt(2, id) ||
            !upd.bindText(3, daFase) || (upd.step(), upd.hasError())) {
            m_lastError = upd.lastError();
            return std::nullopt;
        }
        if (db.changes() != 1)
            return std::nullopt; // perso la corsa: un altro worker l'ha preso
        auto job = leggiPerId(db, id);
        if (!tx.commit()) {
            m_lastError = db.lastError();
            return std::nullopt;
        }
        return job;
    }

    bool PipelineJobStore::setFilePath(std::int64_t id, const std::string& filePath) {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return false;
        }
        auto upd = db.prepare("UPDATE pipeline_jobs SET file_path = ?, "
                              "aggiornato_il = strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                              "WHERE id = ?;");
        if (!upd.valid() || !upd.bindText(1, filePath) || !upd.bindInt(2, id) ||
            (upd.step(), upd.hasError())) {
            m_lastError = upd.lastError();
            return false;
        }
        return db.changes() == 1;
    }

    bool PipelineJobStore::transition(std::int64_t id, const std::string& daFase,
                                      const std::string& aFase, const std::string& errore) {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return false;
        }
        auto upd = db.prepare("UPDATE pipeline_jobs SET fase = ?, errore = ?, "
                              "aggiornato_il = strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                              "WHERE id = ? AND fase = ?;");
        if (!upd.valid() || !upd.bindText(1, aFase) || !upd.bindText(2, errore) ||
            !upd.bindInt(3, id) || !upd.bindText(4, daFase) || (upd.step(), upd.hasError())) {
            m_lastError = upd.lastError();
            return false;
        }
        return db.changes() == 1;
    }

    bool PipelineJobStore::retry(std::int64_t id, std::int64_t delaySec,
                                 const std::string& errore) {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return false;
        }
        auto upd = db.prepare("UPDATE pipeline_jobs SET fase = 'retry_wait', "
                              "tentativi = tentativi + 1, prossima_esecuzione = ?, errore = ?, "
                              "aggiornato_il = strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                              "WHERE id = ? AND fase NOT IN ('completed', 'failed');");
        if (!upd.valid() || !upd.bindInt(1, oraEpoch() + delaySec) || !upd.bindText(2, errore) ||
            !upd.bindInt(3, id) || (upd.step(), upd.hasError())) {
            m_lastError = upd.lastError();
            return false;
        }
        return db.changes() == 1;
    }

    bool PipelineJobStore::retryNow(std::int64_t id) {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return false;
        }
        auto upd = db.prepare("UPDATE pipeline_jobs SET fase = 'queued_download', "
                              "prossima_esecuzione = 0, errore = '', "
                              "aggiornato_il = strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                              "WHERE id = ? AND fase IN ('queued_download', 'failed');");
        if (!upd.valid() || !upd.bindInt(1, id) || (upd.step(), upd.hasError())) {
            m_lastError = upd.lastError();
            return false;
        }
        // Come QueueStore::retryNow: la UPDATE è condizionata alla fase, ma una
        // riga non riprovabile non è un errore di storage (idempotente).
        return true;
    }

    bool PipelineJobStore::fail(std::int64_t id, const std::string& errore) {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return false;
        }
        auto upd = db.prepare("UPDATE pipeline_jobs SET fase = 'failed', errore = ?, "
                              "aggiornato_il = strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                              "WHERE id = ? AND fase NOT IN ('completed', 'failed');");
        if (!upd.valid() || !upd.bindText(1, errore) || !upd.bindInt(2, id) ||
            (upd.step(), upd.hasError())) {
            m_lastError = upd.lastError();
            return false;
        }
        return db.changes() == 1;
    }

    int PipelineJobStore::promuoviRetry(std::int64_t ora) {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return 0;
        }
        const auto adesso = ora > 0 ? ora : oraEpoch();
        auto upd = db.prepare("UPDATE pipeline_jobs SET fase = 'queued_download', "
                              "aggiornato_il = strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                              "WHERE fase = 'retry_wait' AND prossima_esecuzione <= ?;");
        if (!upd.valid() || !upd.bindInt(1, adesso) || (upd.step(), upd.hasError())) {
            m_lastError = upd.lastError();
            return -1;
        }
        return db.changes();
    }

    int PipelineJobStore::resetOrfani() {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return -1;
        }
        Transaction tx(db);
        if (!tx.active()) {
            m_lastError = db.lastError();
            return -1;
        }
        auto ripristina = [this, &db](const std::string& daFase, const std::string& aFase) -> int {
            auto upd = db.prepare("UPDATE pipeline_jobs SET fase = ?, "
                                  "aggiornato_il = strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                                  "WHERE fase = ?;");
            if (!upd.valid() || !upd.bindText(1, aFase) || !upd.bindText(2, daFase) ||
                (upd.step(), upd.hasError())) {
                m_lastError = upd.lastError();
                return -1;
            }
            return db.changes();
        };
        const int download = ripristina("downloading", "queued_download");
        if (download < 0)
            return -1;
        const int conversioni = ripristina("converting", "downloaded_waiting_conversion");
        if (conversioni < 0)
            return -1;
        if (!tx.commit()) {
            m_lastError = db.lastError();
            return -1;
        }
        return download + conversioni;
    }

    std::string PipelineJobStore::beginRun(const std::string& origine) {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return {};
        }
        // Millisecondi + contatore di processo: gli id restano leggibili e
        // unici; l'esecuzione della pipeline è serializzata da PipelineGate,
        // quindi non c'è collisione tra processi concorrenti.
        static std::atomic<std::uint64_t> contatore{0};
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
        const std::string id = "run-" + std::to_string(ms) + "-" +
                               std::to_string(contatore.fetch_add(1, std::memory_order_relaxed));
        const std::string org = origine == "interactive" ? "interactive" : "background";
        auto ins =
            db.prepare("INSERT INTO pipeline_runs(id, origine, stato) VALUES (?, ?, 'in_corso');");
        if (!ins.valid() || !ins.bindText(1, id) || !ins.bindText(2, org) ||
            (ins.step(), ins.hasError())) {
            m_lastError = ins.lastError();
            return {};
        }
        return id;
    }

    bool PipelineJobStore::finishRun(const std::string& id, const std::string& stato) {
        const auto valido = statoRunValido(stato);
        if (valido.empty()) {
            m_lastError = "stato run non valido: " + stato;
            return false;
        }
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return false;
        }
        auto upd = db.prepare("UPDATE pipeline_runs SET stato = ?, "
                              "concluso_il = strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                              "WHERE id = ? AND concluso_il IS NULL;");
        if (!upd.valid() || !upd.bindText(1, valido) || !upd.bindText(2, id) ||
            (upd.step(), upd.hasError())) {
            m_lastError = upd.lastError();
            return false;
        }
        return db.changes() == 1;
    }

    int PipelineJobStore::closeOrphanRuns() {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return -1;
        }
        auto upd = db.prepare("UPDATE pipeline_runs SET stato = 'interrotto', "
                              "concluso_il = strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                              "WHERE concluso_il IS NULL;");
        if (!upd.valid() || (upd.step(), upd.hasError())) {
            m_lastError = upd.lastError();
            return -1;
        }
        return db.changes();
    }

    std::optional<PipelineRun> PipelineJobStore::runSummary(const std::string& id) const {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return std::nullopt;
        }
        auto sel = db.prepare(std::string("SELECT ") + kColonneRun +
                              " FROM pipeline_runs r LEFT JOIN pipeline_jobs j ON j.run_id = r.id "
                              "WHERE r.id = ? GROUP BY r.id;");
        if (!sel.valid() || !sel.bindText(1, id) || !sel.step() || sel.hasError())
            return std::nullopt;
        return leggiRun(sel);
    }

    std::vector<PipelineRun> PipelineJobStore::listRuns(const std::string& window,
                                                        const std::string& origine,
                                                        const std::string& stato, int limit,
                                                        int offset, int* total,
                                                        RunCounts* counts) const {
        std::vector<PipelineRun> out;
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return out;
        }
        if (total)
            *total = 0;
        if (counts)
            *counts = RunCounts{};

        const std::string org = normalizzaOrigine(origine);
        std::string where;
        std::vector<std::string> params;
        auto aggiungi = [&where](const std::string& cond) {
            where += where.empty() ? " WHERE " : " AND ";
            where += cond;
        };
        if (!org.empty()) {
            aggiungi("r.origine = ?");
            params.push_back(org);
        }
        if (stato == "problemi")
            aggiungi("r.stato IN ('completato_con_errori', 'interrotto')");
        else if (stato == "in_corso")
            aggiungi("r.concluso_il IS NULL");
        else {
            const auto st = statoRunValido(stato);
            if (!st.empty()) {
                aggiungi("r.stato = ?");
                params.push_back(st);
            }
        }
        if (window == "recenti")
            aggiungi(std::string("(r.concluso_il IS NULL OR r.concluso_il >= ") + kCutoff24h + ")");
        else if (window == "storico")
            aggiungi(std::string("(r.concluso_il IS NOT NULL AND r.concluso_il < ") + kCutoff24h +
                     ")");

        // Conteggi della vista: solo origine, non window/stato/limit.
        if (counts) {
            std::string sql = std::string("SELECT "
                                          "COALESCE(SUM(CASE WHEN r.concluso_il IS NULL THEN 1 "
                                          "ELSE 0 END), 0), "
                                          "COALESCE(SUM(CASE WHEN r.concluso_il IS NULL OR "
                                          "r.concluso_il >= ") +
                              kCutoff24h +
                              " THEN 1 ELSE 0 END), 0), "
                              "COALESCE(SUM(CASE WHEN r.stato IN ('completato_con_errori', "
                              "'interrotto') THEN 1 ELSE 0 END), 0) "
                              "FROM pipeline_runs r";
            if (!org.empty())
                sql += " WHERE r.origine = ?";
            sql += ";";
            auto sel = db.prepare(sql);
            bool ok = sel.valid();
            if (ok && !org.empty())
                ok = sel.bindText(1, org);
            if (ok && sel.step() && !sel.hasError()) {
                counts->inCorso = static_cast<int>(sel.columnInt(0));
                counts->recenti = static_cast<int>(sel.columnInt(1));
                counts->problemi = static_cast<int>(sel.columnInt(2));
            } else {
                m_lastError = sel.lastError();
            }
        }

        if (total) {
            auto sel = db.prepare("SELECT COUNT(*) FROM pipeline_runs r" + where + ";");
            bool ok = sel.valid();
            for (std::size_t i = 0; ok && i < params.size(); ++i)
                ok = sel.bindText(static_cast<int>(i) + 1, params[i]);
            if (ok && sel.step() && !sel.hasError())
                *total = static_cast<int>(sel.columnInt(0));
            else
                m_lastError = sel.lastError();
        }

        const int cap = limit <= 0 ? 20 : (std::min)(limit, 100);
        const int salto = (std::max)(0, offset);
        auto sel = db.prepare(std::string("SELECT ") + kColonneRun +
                              " FROM pipeline_runs r LEFT JOIN pipeline_jobs j ON j.run_id = r.id" +
                              where +
                              " GROUP BY r.id ORDER BY r.avviato_il DESC, r.rowid DESC "
                              "LIMIT ? OFFSET ?;");
        bool ok = sel.valid();
        int indice = 1;
        for (std::size_t i = 0; ok && i < params.size(); ++i)
            ok = sel.bindText(indice++, params[i]);
        if (ok) {
            ok = sel.bindInt(indice++, cap) && sel.bindInt(indice, salto);
        }
        if (!ok) {
            m_lastError = sel.lastError();
            return out;
        }
        while (sel.step()) {
            if (sel.hasError()) {
                m_lastError = sel.lastError();
                break;
            }
            out.push_back(leggiRun(sel));
        }
        return out;
    }

    std::vector<PipelineJob> PipelineJobStore::runJobs(const std::string& id, int limit) const {
        std::vector<PipelineJob> out;
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return out;
        }
        const int cap = limit <= 0 ? 1000 : (std::min)(limit, 5000);
        auto sel = db.prepare(std::string("SELECT ") + kColonne +
                              " FROM pipeline_jobs WHERE run_id = ? ORDER BY id ASC LIMIT ?;");
        if (!sel.valid() || !sel.bindText(1, id) || !sel.bindInt(2, cap)) {
            m_lastError = sel.lastError();
            return out;
        }
        while (sel.step()) {
            if (sel.hasError()) {
                m_lastError = sel.lastError();
                break;
            }
            out.push_back(leggiRiga(sel));
        }
        return out;
    }

    int PipelineJobStore::retryFailedRun(const std::string& id) {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return -1;
        }
        auto upd = db.prepare("UPDATE pipeline_jobs SET fase = 'queued_download', "
                              "prossima_esecuzione = 0, errore = '', "
                              "aggiornato_il = strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                              "WHERE run_id = ? AND fase = 'failed';");
        if (!upd.valid() || !upd.bindText(1, id) || (upd.step(), upd.hasError())) {
            m_lastError = upd.lastError();
            return -1;
        }
        return db.changes();
    }

    bool PipelineJobStore::incrementSkipped(const std::string& id) {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return false;
        }
        auto upd = db.prepare("UPDATE pipeline_runs SET saltati = saltati + 1 WHERE id = ?;");
        if (!upd.valid() || !upd.bindText(1, id) || (upd.step(), upd.hasError())) {
            m_lastError = upd.lastError();
            return false;
        }
        return db.changes() == 1;
    }

    std::vector<PipelineJob> PipelineJobStore::list(const std::string& fase, int limit) const {
        JobQuery query;
        query.fase = fase;
        query.limit = limit;
        return list(query);
    }

    std::vector<PipelineJob> PipelineJobStore::list(const JobQuery& query) const {
        std::vector<PipelineJob> out;
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return out;
        }
        std::string sql = std::string("SELECT ") + kColonne + " FROM pipeline_jobs";
        std::vector<std::string> params;
        bool dove = false;
        auto aggiungi = [&sql, &dove](const std::string& cond) {
            sql += dove ? " AND " : " WHERE ";
            dove = true;
            sql += cond;
        };
        if (query.fase == "problemi")
            aggiungi("fase IN ('failed', 'interrupted')");
        else if (!query.fase.empty()) {
            aggiungi("fase = ?");
            params.push_back(query.fase);
        }
        if (!query.runId.empty()) {
            aggiungi("run_id = ?");
            params.push_back(query.runId);
        }
        if (query.window == "recenti")
            aggiungi(std::string("(fase NOT IN ('completed', 'failed', 'interrupted') OR "
                                 "aggiornato_il >= ") +
                     kCutoff24h + ")");
        else if (query.window == "storico")
            aggiungi(std::string("(fase IN ('completed', 'failed', 'interrupted') AND "
                                 "aggiornato_il < ") +
                     kCutoff24h + ")");
        sql += " ORDER BY id DESC LIMIT ? OFFSET ?;";
        const int cap = query.limit <= 0 ? 100 : (std::min)(query.limit, 500);
        const int salto = (std::max)(0, query.offset);
        auto sel = db.prepare(sql);
        bool ok = sel.valid();
        int indice = 1;
        for (std::size_t i = 0; ok && i < params.size(); ++i)
            ok = sel.bindText(indice++, params[i]);
        if (ok)
            ok = sel.bindInt(indice++, cap) && sel.bindInt(indice, salto);
        if (!ok) {
            m_lastError = sel.lastError();
            return out;
        }
        while (sel.step()) {
            if (sel.hasError()) {
                m_lastError = sel.lastError();
                break;
            }
            out.push_back(leggiRiga(sel));
        }
        return out;
    }

    std::map<std::string, int> PipelineJobStore::counts() const {
        std::map<std::string, int> out;
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return out;
        }
        auto sel = db.prepare("SELECT fase, COUNT(*) FROM pipeline_jobs GROUP BY fase;");
        if (!sel.valid()) {
            m_lastError = sel.lastError();
            return out;
        }
        while (sel.step()) {
            if (sel.hasError()) {
                m_lastError = sel.lastError();
                break;
            }
            out[sel.columnText(0)] = static_cast<int>(sel.columnInt(1));
        }
        return out;
    }

} // namespace Core
