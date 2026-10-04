#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

// Wrapper RAII sopra SQLite vendored (third_party/sqlite). Niente eccezioni
// oltre i confini: ogni operazione ritorna bool + messaggio d'errore.
// Thread-safe quanto SQLite in serialized mode (default): un Database per
// thread, oppure handle distinti aperti sullo stesso file.
namespace Core {

    class Database;

    // Statement preparato: binding per indice 1-based (convenzione SQLite),
    // step() avanza e ritorna true finché c'è una riga da leggere.
    class Statement {
      public:
        Statement() = default;
        Statement(const Statement&) = delete;
        Statement& operator=(const Statement&) = delete;
        Statement(Statement&& other) noexcept;
        Statement& operator=(Statement&& other) noexcept;
        ~Statement();

        [[nodiscard]] bool valid() const noexcept;
        [[nodiscard]] std::string lastError() const;

        bool bindInt(int index, std::int64_t value);
        bool bindDouble(int index, double value);
        bool bindText(int index, const std::string& value);
        bool bindNull(int index);

        // true = riga disponibile, false = fine (o errore: vedi lastError()).
        bool step();
        [[nodiscard]] bool hasError() const noexcept;

        [[nodiscard]] int columnCount() const noexcept;
        [[nodiscard]] std::string columnName(int col) const;
        [[nodiscard]] bool columnIsNull(int col) const;
        [[nodiscard]] std::int64_t columnInt(int col) const;
        [[nodiscard]] double columnDouble(int col) const;
        [[nodiscard]] std::string columnText(int col) const;

        bool reset();

      private:
        friend class Database;
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

    // Transazione RAII: rollback automatico alla distruzione se commit()
    // non è stato chiamato con successo.
    class Transaction {
      public:
        explicit Transaction(Database& db);
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;
        ~Transaction();

        bool commit();
        [[nodiscard]] bool active() const noexcept;

      private:
        Database* m_db;
        bool m_active{false};
    };

    class Database {
      public:
        static constexpr int kBusyTimeoutMs = 5000;
        static constexpr int kSchemaVersion = 2;
        // Apertura concorrente sullo stesso file fresco: PRAGMA journal_mode
        // può rispondere SQLITE_BUSY senza invocare il busy handler.
        static constexpr int kOpenRetries = 30;
        static constexpr int kOpenRetryMs = 50;

        explicit Database(const std::filesystem::path& dbPath);
        Database(const Database&) = delete;
        Database& operator=(const Database&) = delete;
        ~Database();

        [[nodiscard]] bool isOpen() const noexcept;
        [[nodiscard]] std::string lastError() const;
        [[nodiscard]] int schemaVersion() const noexcept;

        bool execute(const std::string& sql, std::string* error = nullptr);
        Statement prepare(const std::string& sql);

        // Applica le migrazioni mancanti fino a kSchemaVersion (idempotente).
        bool migrate(std::string* error = nullptr);

      private:
        friend class Transaction;
        struct Impl;
        std::unique_ptr<Impl> m_impl;

        // Un tentativo di apertura + PRAGMA + migrazioni. Ritorna false con
        // lastError() valorizzato; errBusy=true se il fallimento è un lock
        // transitorio (SQLITE_BUSY) che merita un nuovo tentativo.
        bool openOnce(const std::filesystem::path& dbPath, bool& errBusy);
    };

} // namespace Core
