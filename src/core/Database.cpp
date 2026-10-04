#include "core/Database.hpp"

#include "sqlite3.h"

#include <chrono>
#include <thread>
#include <utility>

namespace Core {

    namespace {
        // Schema v1: series rispecchia 1:1 i campi del JSON (chiave = name,
        // come in SeriesRepository::applyDownloadedEpisodes). Le fonti
        // alternate restano un sotto-documento JSON: tabella separata
        // pagherebbe complessità senza query che la richiedano.
        constexpr const char* kSchemaV1 = R"(
CREATE TABLE IF NOT EXISTS migrations (
    version    INTEGER PRIMARY KEY,
    applied_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))
);
CREATE TABLE IF NOT EXISTS series (
    name                   TEXT PRIMARY KEY,
    service                TEXT NOT NULL DEFAULT '',
    path                   TEXT NOT NULL DEFAULT '',
    continue_series        INTEGER NOT NULL DEFAULT 1,
    is_high_priority       INTEGER NOT NULL DEFAULT 0,
    passed_episodes        INTEGER NOT NULL DEFAULT 0,
    series_page_url        TEXT NOT NULL DEFAULT '',
    episode_list_selector  TEXT NOT NULL DEFAULT '',
    download_link_selector TEXT NOT NULL DEFAULT '',
    last_downloaded_at     TEXT NOT NULL DEFAULT '',
    last_downloaded_episode INTEGER NOT NULL DEFAULT 0,
    alternate_sources      TEXT NOT NULL DEFAULT '[]'
);
CREATE TABLE IF NOT EXISTS kv (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL DEFAULT ''
);
)";
        // Schema v2: + coda task persistente (B4/B6, ADR-004). Gli stati sono
        // testo vincolato da CHECK: attesa = da eseguire, attivo = in mano a
        // un worker (reset ad attesa a ogni avvio: crash recovery), fatto e
        // fallito = terminali. Epoch unix in prossima_esecuzione per retry
        // con backoff senza svegliare nessuno.
        constexpr const char* kSchemaV2 = R"(
CREATE TABLE IF NOT EXISTS tasks (
    id                 INTEGER PRIMARY KEY AUTOINCREMENT,
    kind               TEXT NOT NULL DEFAULT 'check',
    serie              TEXT NOT NULL DEFAULT '',
    episodio           INTEGER NOT NULL DEFAULT 0,
    payload            TEXT NOT NULL DEFAULT '{}',
    stato              TEXT NOT NULL DEFAULT 'attesa'
                       CHECK (stato IN ('attesa', 'attivo', 'fatto', 'fallito')),
    tentativi          INTEGER NOT NULL DEFAULT 0,
    prossima_esecuzione INTEGER NOT NULL DEFAULT 0,
    priorita           INTEGER NOT NULL DEFAULT 0,
    creato_il          TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))
);
CREATE INDEX IF NOT EXISTS idx_tasks_claim
    ON tasks(stato, prossima_esecuzione, priorita DESC, id);
)";
    } // namespace

    struct Statement::Impl {
        sqlite3* db{nullptr};
        sqlite3_stmt* stmt{nullptr};
        bool error{false};
    };

    struct Database::Impl {
        sqlite3* db{nullptr};
        std::string lastError;
        int schemaVersion{0};
    };

    Statement::Statement(Statement&& other) noexcept : m_impl(std::move(other.m_impl)) {}
    Statement& Statement::operator=(Statement&& other) noexcept {
        m_impl = std::move(other.m_impl);
        return *this;
    }

    Statement::~Statement() {
        if (m_impl && m_impl->stmt)
            sqlite3_finalize(m_impl->stmt);
    }

    bool Statement::valid() const noexcept { return m_impl && m_impl->stmt; }

    std::string Statement::lastError() const {
        if (!m_impl || !m_impl->db)
            return "statement non valido";
        return sqlite3_errmsg(m_impl->db);
    }

    bool Statement::bindInt(int index, std::int64_t value) {
        if (!valid())
            return false;
        if (sqlite3_bind_int64(m_impl->stmt, index, value) != SQLITE_OK) {
            m_impl->error = true;
            return false;
        }
        return true;
    }

    bool Statement::bindDouble(int index, double value) {
        if (!valid())
            return false;
        if (sqlite3_bind_double(m_impl->stmt, index, value) != SQLITE_OK) {
            m_impl->error = true;
            return false;
        }
        return true;
    }

    bool Statement::bindText(int index, const std::string& value) {
        if (!valid())
            return false;
        // SQLITE_TRANSIENT è l'API documentata di SQLite (macro = (void*)-1):
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        if (sqlite3_bind_text(m_impl->stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) !=
            SQLITE_OK) {
            m_impl->error = true;
            return false;
        }
        return true;
    }

    bool Statement::bindNull(int index) {
        if (!valid())
            return false;
        if (sqlite3_bind_null(m_impl->stmt, index) != SQLITE_OK) {
            m_impl->error = true;
            return false;
        }
        return true;
    }

    bool Statement::step() {
        if (!valid())
            return false;
        const int rc = sqlite3_step(m_impl->stmt);
        if (rc == SQLITE_ROW)
            return true;
        if (rc != SQLITE_DONE)
            m_impl->error = true;
        return false;
    }

    bool Statement::hasError() const noexcept { return !m_impl || m_impl->error; }

    int Statement::columnCount() const noexcept {
        return valid() ? sqlite3_column_count(m_impl->stmt) : 0;
    }

    std::string Statement::columnName(int col) const {
        if (!valid())
            return {};
        const char* name = sqlite3_column_name(m_impl->stmt, col);
        return name ? name : "";
    }

    bool Statement::columnIsNull(int col) const {
        return !valid() || sqlite3_column_type(m_impl->stmt, col) == SQLITE_NULL;
    }

    std::int64_t Statement::columnInt(int col) const {
        return valid() ? sqlite3_column_int64(m_impl->stmt, col) : 0;
    }

    double Statement::columnDouble(int col) const {
        return valid() ? sqlite3_column_double(m_impl->stmt, col) : 0.0;
    }

    std::string Statement::columnText(int col) const {
        if (!valid())
            return {};
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(m_impl->stmt, col));
        return text ? text : "";
    }

    bool Statement::reset() {
        if (!valid())
            return false;
        m_impl->error = false;
        return sqlite3_reset(m_impl->stmt) == SQLITE_OK &&
               sqlite3_clear_bindings(m_impl->stmt) == SQLITE_OK;
    }

    Database::Database(const std::filesystem::path& dbPath) : m_impl(std::make_unique<Impl>()) {
        // Retry sui lock transitori: chiude la race d'apertura simultanea
        // (vedi openOnce). Errori veri (permessi, disco) escono subito.
        for (int tentativo = 0; tentativo < kOpenRetries; ++tentativo) {
            bool errBusy = false;
            if (openOnce(dbPath, errBusy))
                return;
            if (!errBusy || tentativo + 1 == kOpenRetries)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(kOpenRetryMs));
        }
    }

    bool Database::openOnce(const std::filesystem::path& dbPath, bool& errBusy) {
        errBusy = false;
        std::error_code ec;
        if (!dbPath.parent_path().empty())
            std::filesystem::create_directories(dbPath.parent_path(), ec);
        if (ec) {
            m_impl->lastError = "creazione directory: " + ec.message();
            return false;
        }
        if (sqlite3_open_v2(dbPath.string().c_str(), &m_impl->db,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
            errBusy = sqlite3_errcode(m_impl->db) == SQLITE_BUSY;
            m_impl->lastError = m_impl->db ? sqlite3_errmsg(m_impl->db) : "apertura fallita";
            if (m_impl->db) {
                sqlite3_close(m_impl->db);
                m_impl->db = nullptr;
            }
            return false;
        }
        // busy-timeout SUBITO dopo open, prima di qualunque PRAGMA.
        sqlite3_busy_timeout(m_impl->db, kBusyTimeoutMs);
        std::string error;
        // WAL: lettori non bloccano lo scrittore (web UI + worker convivono).
        // synchronous=NORMAL sotto WAL è crash-safe senza fsync a ogni commit.
        // NOTA: PRAGMA journal_mode su file fresco con aperture simultanee
        // può rispondere SQLITE_BUSY senza invocare il busy handler: il retry
        // del costruttore copre il caso, qui basta segnalarlo (errBusy).
        auto fallito = [&](const std::string& fase) {
            errBusy = sqlite3_errcode(m_impl->db) == SQLITE_BUSY;
            m_impl->lastError = fase + ": " + error;
            sqlite3_close(m_impl->db);
            m_impl->db = nullptr;
            return false;
        };
        if (!execute("PRAGMA journal_mode=WAL;", &error) ||
            !execute("PRAGMA synchronous=NORMAL;", &error) ||
            !execute("PRAGMA foreign_keys=ON;", &error))
            return fallito("pragma");
        if (!migrate(&error))
            return fallito("migrate");
        return true;
    }

    Database::~Database() {
        if (m_impl && m_impl->db)
            sqlite3_close(m_impl->db);
    }

    bool Database::isOpen() const noexcept { return m_impl && m_impl->db; }

    std::string Database::lastError() const {
        if (!m_impl)
            return "database non inizializzato";
        if (!m_impl->lastError.empty())
            return m_impl->lastError;
        return m_impl->db ? sqlite3_errmsg(m_impl->db) : "database chiuso";
    }

    int Database::schemaVersion() const noexcept { return m_impl ? m_impl->schemaVersion : 0; }

    bool Database::execute(const std::string& sql, std::string* error) {
        if (!isOpen()) {
            if (error)
                *error = lastError();
            return false;
        }
        char* errMsg = nullptr;
        if (sqlite3_exec(m_impl->db, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
            if (error)
                *error = errMsg ? errMsg : "errore SQL";
            sqlite3_free(errMsg);
            return false;
        }
        return true;
    }

    Statement Database::prepare(const std::string& sql) {
        Statement stmt;
        if (!isOpen())
            return stmt;
        stmt.m_impl = std::make_unique<Statement::Impl>();
        stmt.m_impl->db = m_impl->db;
        if (sqlite3_prepare_v2(m_impl->db, sql.c_str(), -1, &stmt.m_impl->stmt, nullptr) !=
            SQLITE_OK)
            stmt.m_impl->error = true;
        return stmt;
    }

    bool Database::migrate(std::string* error) {
        if (!isOpen()) {
            if (error)
                *error = lastError();
            return false;
        }
        Transaction tx(*this);
        if (!tx.active()) {
            if (error)
                *error = lastError();
            return false;
        }
        // Migrazioni in ordine di versione: ogni schema è idempotente
        // (IF NOT EXISTS) e registra la sua versione; DB fermi a v1
        // prendono solo la v2, DB nuovi prendono tutto.
        int attuale = 0;
        {
            // Scope: statement finalizzato prima dei DDL (stessa lezione dei
            // cursori aperti prima del COMMIT).
            auto versione = prepare("SELECT MAX(version) FROM migrations;");
            if (versione.valid() && versione.step() && !versione.hasError())
                attuale = static_cast<int>(versione.columnInt(0));
        }
        auto applica = [&](int v, const char* schema) {
            if (attuale >= v)
                return true;
            if (!execute(schema, error))
                return false;
            auto insert = prepare("INSERT OR IGNORE INTO migrations(version) VALUES (?);");
            if (!insert.valid() || !insert.bindInt(1, v) || (insert.step(), insert.hasError())) {
                if (error)
                    *error = insert.lastError();
                return false;
            }
            attuale = v;
            return true;
        };
        if (!applica(1, kSchemaV1) || !applica(2, kSchemaV2))
            return false;
        m_impl->schemaVersion = attuale;
        if (!tx.commit()) {
            if (error)
                *error = lastError();
            return false;
        }
        return true;
    }

    Transaction::Transaction(Database& db) : m_db(&db) {
        std::string error;
        m_active = m_db->isOpen() && m_db->execute("BEGIN IMMEDIATE;", &error);
        if (!m_active)
            m_db->m_impl->lastError = error;
    }

    Transaction::~Transaction() {
        if (m_active && m_db->isOpen()) {
            std::string error;
            m_db->execute("ROLLBACK;", &error);
        }
    }

    bool Transaction::commit() {
        if (!m_active)
            return false;
        std::string error;
        if (!m_db->execute("COMMIT;", &error)) {
            m_db->m_impl->lastError = error;
            return false;
        }
        m_active = false;
        return true;
    }

    bool Transaction::active() const noexcept { return m_active; }

} // namespace Core
