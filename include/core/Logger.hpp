#pragma once
#include <string>
#include <mutex>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace Core {

    /**
     * @brief Logger centralizzato per tutto il progetto.
     * Tutti i messaggi vengono scritti in un unico file di log con timestamp.
     * Thread-safe: può essere usato da più thread contemporaneamente.
     * Stream persistente aperto in init(): la scrittura per riga non riapre
     * il file; la rotazione (>5MB) è verificata con throttle di 1s.
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
        static std::ofstream s_logStream;
        static std::chrono::steady_clock::time_point s_lastRotCheck;
    };

} // namespace Core
