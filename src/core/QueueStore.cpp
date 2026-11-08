#include "core/QueueStore.hpp"

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
    } // namespace

    QueueStore::QueueStore(const std::filesystem::path& dbPath) : m_dbPath(dbPath) {}

    bool QueueStore::isOpen() const {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return false;
        }
        return true;
    }

    std::string QueueStore::lastError() const { return m_lastError; }

    bool QueueStore::resetOrfani() {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return false;
        }
        std::string error;
        if (!db.execute("UPDATE tasks SET stato = 'attesa' WHERE stato = 'attivo';", &error)) {
            m_lastError = error;
            return false;
        }
        return true;
    }

    std::int64_t QueueStore::enqueue(const std::string& kind, const std::string& serie,
                                     int episodio, const std::string& payload, int priorita,
                                     std::int64_t ritardoSec) {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return 0;
        }
        auto ins = db.prepare("INSERT INTO tasks(kind, serie, episodio, payload, priorita, "
                              "prossima_esecuzione) VALUES (?, ?, ?, ?, ?, ?);");
        if (!ins.valid() || !ins.bindText(1, kind) || !ins.bindText(2, serie) ||
            !ins.bindInt(3, episodio) || !ins.bindText(4, payload) || !ins.bindInt(5, priorita) ||
            !ins.bindInt(6, oraEpoch() + ritardoSec) || (ins.step(), ins.hasError())) {
            m_lastError = ins.lastError();
            return 0;
        }
        auto id = db.prepare("SELECT last_insert_rowid();");
        if (!id.valid() || !id.step() || id.hasError()) {
            m_lastError = id.lastError();
            return 0;
        }
        return id.columnInt(0);
    }

    std::optional<QueueTask> QueueStore::claim() {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return std::nullopt;
        }
        Transaction tx(db);
        if (!tx.active()) {
            m_lastError = db.lastError();
            return std::nullopt;
        }
        auto sel =
            db.prepare("SELECT id, kind, serie, episodio, payload, tentativi, priorita FROM tasks "
                       "WHERE stato = 'attesa' AND prossima_esecuzione <= ? "
                       "ORDER BY priorita DESC, id LIMIT 1;");
        if (!sel.valid() || !sel.bindInt(1, oraEpoch())) {
            m_lastError = sel.lastError();
            return std::nullopt;
        }
        if (!sel.step() || sel.hasError()) {
            if (sel.hasError())
                m_lastError = sel.lastError();
            return std::nullopt; // coda vuota (rollback implicito)
        }
        QueueTask task;
        task.id = sel.columnInt(0);
        task.kind = sel.columnText(1);
        task.serie = sel.columnText(2);
        task.episodio = static_cast<int>(sel.columnInt(3));
        task.payload = sel.columnText(4);
        task.tentativi = static_cast<int>(sel.columnInt(5));
        task.priorita = static_cast<int>(sel.columnInt(6));
        auto upd =
            db.prepare("UPDATE tasks SET stato = 'attivo' WHERE id = ? AND stato = 'attesa';");
        if (!upd.valid() || !upd.bindInt(1, task.id) || (upd.step(), upd.hasError())) {
            m_lastError = upd.lastError();
            return std::nullopt;
        }
        // Verifica che l'UPDATE abbia toccato davvero una riga: se un altro
        // worker ci ha battuto sul tempo, changes()=0 e rinunciamo.
        auto check = db.prepare("SELECT changes();");
        if (!check.valid() || !check.step() || check.hasError() || check.columnInt(0) != 1)
            return std::nullopt;
        if (!tx.commit()) {
            m_lastError = db.lastError();
            return std::nullopt;
        }
        return task;
    }

    bool QueueStore::complete(std::int64_t id) {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return false;
        }
        auto upd = db.prepare("UPDATE tasks SET stato = 'fatto' WHERE id = ?;");
        if (!upd.valid() || !upd.bindInt(1, id) || (upd.step(), upd.hasError())) {
            m_lastError = upd.lastError();
            return false;
        }
        return true;
    }

    bool QueueStore::failRetry(std::int64_t id, std::int64_t attesaSec) {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return false;
        }
        auto upd = db.prepare("UPDATE tasks SET stato = 'attesa', tentativi = tentativi + 1, "
                              "prossima_esecuzione = ? WHERE id = ?;");
        if (!upd.valid() || !upd.bindInt(1, oraEpoch() + attesaSec) || !upd.bindInt(2, id) ||
            (upd.step(), upd.hasError())) {
            m_lastError = upd.lastError();
            return false;
        }
        return true;
    }

    bool QueueStore::failDead(std::int64_t id) {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return false;
        }
        auto upd = db.prepare("UPDATE tasks SET stato = 'fallito' WHERE id = ?;");
        if (!upd.valid() || !upd.bindInt(1, id) || (upd.step(), upd.hasError())) {
            m_lastError = upd.lastError();
            return false;
        }
        return true;
    }

    std::int64_t QueueStore::pendingCount(const std::string& kind) {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return -1;
        }
        auto sel =
            kind.empty()
                ? db.prepare("SELECT COUNT(*) FROM tasks WHERE stato = 'attesa';")
                : db.prepare("SELECT COUNT(*) FROM tasks WHERE stato = 'attesa' AND kind = ?;");
        if (!sel.valid() || (!kind.empty() && !sel.bindText(1, kind)) || !sel.step() ||
            sel.hasError()) {
            m_lastError = sel.lastError();
            return -1;
        }
        return sel.columnInt(0);
    }

    std::vector<QueueStore::Voce> QueueStore::list(const std::string& stato, int limit) const {
        std::vector<Voce> out;
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return out;
        }
        const int cap = (limit <= 0) ? 100 : (std::min)(limit, 500);
        const std::string colonne = "SELECT id, kind, serie, episodio, stato, tentativi, "
                                    "prossima_esecuzione, priorita, creato_il FROM tasks ";
        const std::string ordine =
            " ORDER BY CASE stato WHEN 'attivo' THEN 0 WHEN 'attesa' THEN 1 "
            "WHEN 'fallito' THEN 2 ELSE 3 END, priorita DESC, id DESC LIMIT ?;";
        auto sel = stato.empty() ? db.prepare(colonne + "WHERE stato != 'fatto'" + ordine)
                                 : db.prepare(colonne + "WHERE stato = ?" + ordine);
        if (!sel.valid() || (!stato.empty() && !sel.bindText(1, stato)) ||
            !sel.bindInt(stato.empty() ? 1 : 2, cap)) {
            m_lastError = sel.lastError();
            return out;
        }
        while (sel.step()) {
            if (sel.hasError()) {
                m_lastError = sel.lastError();
                break;
            }
            Voce v;
            v.id = sel.columnInt(0);
            v.kind = sel.columnText(1);
            v.serie = sel.columnText(2);
            v.episodio = static_cast<int>(sel.columnInt(3));
            v.stato = sel.columnText(4);
            v.tentativi = static_cast<int>(sel.columnInt(5));
            v.prossimaEsecuzione = sel.columnInt(6);
            v.priorita = static_cast<int>(sel.columnInt(7));
            v.creatoIl = sel.columnText(8);
            out.push_back(std::move(v));
        }
        return out;
    }

    std::map<std::string, int> QueueStore::counts() const {
        std::map<std::string, int> out;
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return out;
        }
        auto sel = db.prepare("SELECT stato, COUNT(*) FROM tasks GROUP BY stato;");
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

    bool QueueStore::retryNow(std::int64_t id) {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return false;
        }
        auto upd = db.prepare("UPDATE tasks SET stato = 'attesa', prossima_esecuzione = 0 "
                              "WHERE id = ? AND stato IN ('attesa', 'fallito');");
        if (!upd.valid() || !upd.bindInt(1, id) || (upd.step(), upd.hasError())) {
            m_lastError = upd.lastError();
            return false;
        }
        return true;
    }

} // namespace Core
