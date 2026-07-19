#pragma once
#include <string>
#include <mutex>
#include <filesystem>

namespace Core {

    /**
     * @brief Logger centralizzato per tutto il progetto.
     * Tutti i messaggi vengono scritti in un unico file di log con timestamp.
     * Thread-safe: può essere usato da più thread contemporaneamente.
     */
    class Logger {
    public:
        static void init(const std::string& logFilePath);
        static void info(const std::string& msg);
        static void warn(const std::string& msg);
        static void error(const std::string& msg);
        static void result(const std::string& seriesName, double dlTime, double convTime);

    private:
        static void write(const std::string& level, const std::string& msg);
        static std::filesystem::path s_logPath;
        static std::mutex s_mutex;
    };

} // namespace Core
