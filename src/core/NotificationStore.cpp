#include "core/NotificationStore.hpp"

#include "core/Database.hpp"

#include <algorithm>
#include <utility>

namespace Core {

    NotificationStore::NotificationStore(std::filesystem::path dbPath)
        : m_dbPath(std::move(dbPath)) {}

    std::string NotificationStore::lastError() const { return m_lastError; }

    bool NotificationStore::add(const std::string& serie, int episodio, std::int64_t* idOut) {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return false;
        }
        // Deduplica per (serie, episodio): un episodio già notificato non
        // genera un secondo badge/notifica OS. idOut = 0 segnala "già
        // presente"; false resta riservato agli errori di storage.
        auto ins = db.prepare("INSERT INTO notifications(serie, episodio) "
                              "SELECT ?, ? WHERE NOT EXISTS ("
                              "SELECT 1 FROM notifications WHERE serie = ? AND episodio = ?);");
        if (!ins.valid() || !ins.bindText(1, serie) || !ins.bindInt(2, episodio) ||
            !ins.bindText(3, serie) || !ins.bindInt(4, episodio) || (ins.step(), ins.hasError())) {
            m_lastError = ins.lastError();
            return false;
        }
        auto changes = db.prepare("SELECT changes();");
        if (!changes.valid() || !changes.step() || changes.hasError()) {
            m_lastError = changes.lastError();
            return false;
        }
        const auto inserite = changes.columnInt(0);
        if (idOut != nullptr) {
            if (inserite == 0) {
                *idOut = 0;
                return true;
            }
            auto id = db.prepare("SELECT last_insert_rowid();");
            if (!id.valid() || !id.step() || id.hasError()) {
                m_lastError = id.lastError();
                return false;
            }
            *idOut = id.columnInt(0);
        }
        return true;
    }

    std::vector<Notifica> NotificationStore::list(int limit) const {
        std::vector<Notifica> out;
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return out;
        }
        const int cap = (limit <= 0) ? kDefaultLimit : (std::min)(limit, kMaxLimit);
        // Il LIMIT non accetta un parametro nel dialetto usato dal wrapper
        // (bind su LIMIT è supportato da SQLite, ma il valore è già validato
        // e intero: nessuna interpolazione di testo esterno).
        auto sel = db.prepare("SELECT id, serie, episodio, creato_il, letta FROM notifications "
                              "ORDER BY id DESC LIMIT ?;");
        if (!sel.valid() || !sel.bindInt(1, cap)) {
            m_lastError = sel.lastError();
            return out;
        }
        while (sel.step()) {
            if (sel.hasError()) {
                m_lastError = sel.lastError();
                break;
            }
            Notifica n;
            n.id = sel.columnInt(0);
            n.serie = sel.columnText(1);
            n.episodio = static_cast<int>(sel.columnInt(2));
            n.creatoIl = sel.columnText(3);
            n.letta = sel.columnInt(4) != 0;
            out.push_back(std::move(n));
        }
        return out;
    }

    int NotificationStore::unreadCount() const {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return -1;
        }
        auto sel = db.prepare("SELECT COUNT(*) FROM notifications WHERE letta = 0;");
        if (!sel.valid() || !sel.step() || sel.hasError()) {
            m_lastError = sel.lastError();
            return -1;
        }
        return static_cast<int>(sel.columnInt(0));
    }

    bool NotificationStore::markRead(std::int64_t id) {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return false;
        }
        auto upd = db.prepare("UPDATE notifications SET letta = 1 WHERE id = ?;");
        if (!upd.valid() || !upd.bindInt(1, id) || (upd.step(), upd.hasError())) {
            m_lastError = upd.lastError();
            return false;
        }
        return true;
    }

    bool NotificationStore::markAllRead() {
        Database db(m_dbPath);
        if (!db.isOpen()) {
            m_lastError = db.lastError();
            return false;
        }
        std::string error;
        if (!db.execute("UPDATE notifications SET letta = 1 WHERE letta = 0;", &error)) {
            m_lastError = error;
            return false;
        }
        return true;
    }

} // namespace Core
