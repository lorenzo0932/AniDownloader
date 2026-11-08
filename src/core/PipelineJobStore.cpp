#include "core/PipelineJobStore.hpp"

#include "core/Database.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace Core {

    namespace {
        std::int64_t oraEpoch() {
            return std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }

        constexpr const char* kColonne =
            "id, job_key, serie, episodio, origine, fase, tentativi, prossima_esecuzione, "
            "priorita, errore, creato_il, aggiornato_il";

        PipelineJob leggiRiga(Statement& sel) {
            PipelineJob j;
            j.id = sel.columnInt(0);
            j.jobKey = sel.columnText(1);
            j.serie = sel.columnText(2);
            j.episodio = static_cast<int>(sel.columnInt(3));
            j.origine = sel.columnText(4);
            j.fase = sel.columnText(5);
            j.tentativi = static_cast<int>(sel.columnInt(6));
            j.prossimaEsecuzione = sel.columnInt(7);
            j.priorita = static_cast<int>(sel.columnInt(8));
            j.errore = sel.columnText(9);
            j.creatoIl = sel.columnText(10);
            j.aggiornatoIl = sel.columnText(11);
            return j;
        }

        std::optional<PipelineJob> leggiPerId(Database& db, std::int64_t id) {
            auto sel =
                db.prepare(std::string("SELECT ") + kColonne + " FROM pipeline_jobs WHERE id = ?;");
            if (!sel.valid() || !sel.bindInt(1, id) || !sel.step() || sel.hasError())
                return std::nullopt;
            return leggiRiga(sel);
        }
    } // namespace

    PipelineJobStore::PipelineJobStore(std::filesystem::path dbPath)
        : m_dbPath(std::move(dbPath)) {}

    std::string PipelineJobStore::lastError() const { return m_lastError; }

    PipelineJobStore::EsitoEnqueue PipelineJobStore::enqueue(const std::string& jobKey,
                                                             const std::string& serie, int episodio,
                                                             const std::string& origine,
                                                             int priorita) {
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
            if (interattivo && origineEsistente != "interactive") {
                auto upd = db.prepare("UPDATE pipeline_jobs SET origine = 'interactive', "
                                      "priorita = MAX(priorita, ?), "
                                      "aggiornato_il = strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                                      "WHERE id = ?;");
                if (!upd.valid() || !upd.bindInt(1, (std::max)(priorita, 100)) ||
                    !upd.bindInt(2, esito.id) || (upd.step(), upd.hasError())) {
                    m_lastError = upd.lastError();
                    return esito;
                }
                esito.promosso = true;
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
                              "priorita) VALUES (?, ?, ?, ?, ?);");
        if (!ins.valid() || !ins.bindText(1, jobKey) || !ins.bindText(2, serie) ||
            !ins.bindInt(3, episodio) || !ins.bindText(4, origine) || !ins.bindInt(5, priorita) ||
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

    std::vector<PipelineJob> PipelineJobStore::list(const std::string& fase, int limit) const {
        std::vector<PipelineJob> out;
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return out;
        }
        const int cap = (limit <= 0) ? 100 : (std::min)(limit, 500);
        std::string sql = std::string("SELECT ") + kColonne + " FROM pipeline_jobs";
        if (!fase.empty())
            sql += " WHERE fase = ?";
        sql += " ORDER BY id DESC LIMIT ?;";
        auto sel = db.prepare(sql);
        if (!sel.valid() || (!fase.empty() && !sel.bindText(1, fase)) ||
            !sel.bindInt(fase.empty() ? 1 : 2, cap)) {
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
