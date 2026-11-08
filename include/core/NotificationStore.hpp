#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Notifiche "pronto da guardare": ogni episodio scaricato dal giro
// schedulato produce una riga persistente. La tabella vive nello stesso
// series_data.db delle serie (schema v3), quindi il badge sopravvive al
// riavvio del demone. Ogni chiamata apre la propria connessione, come
// QueueStore: mai condividere un handle tra thread.
namespace Core {

    struct Notifica {
        std::int64_t id{0};
        std::string serie;
        int episodio{0};
        std::string creatoIl;
        bool letta{false};
    };

    class NotificationStore {
      public:
        explicit NotificationStore(std::filesystem::path dbPath);

        // Inserisce; idOut (opzionale) riceve l'id generato.
        bool add(const std::string& serie, int episodio, std::int64_t* idOut = nullptr);

        // Più recenti prima. limit <= 0 usa kDefaultLimit.
        [[nodiscard]] std::vector<Notifica> list(int limit = 50) const;

        [[nodiscard]] int unreadCount() const;

        bool markRead(std::int64_t id);
        bool markAllRead();

        [[nodiscard]] std::string lastError() const;

        static constexpr int kDefaultLimit = 50;
        static constexpr int kMaxLimit = 500;

      private:
        std::filesystem::path m_dbPath;
        mutable std::string m_lastError;
    };

} // namespace Core
