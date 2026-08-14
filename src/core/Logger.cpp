#include "core/Logger.hpp"
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace Core {

std::filesystem::path Logger::s_logPath;
std::mutex Logger::s_mutex;
std::ofstream Logger::s_logStream;
std::chrono::steady_clock::time_point Logger::s_lastRotCheck{};

static constexpr std::uintmax_t MAX_LOG_SIZE = 5 * 1024 * 1024; // 5 MB

void Logger::init(const std::string& logFilePath) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_logPath = logFilePath;
    try {
        std::filesystem::create_directories(s_logPath.parent_path());
        if (s_logStream.is_open()) s_logStream.close();
        s_logStream.open(s_logPath, std::ios::app);
    } catch (...) {} // intenzionale: il logger non deve mai lanciare (rete di sicurezza)
}

void Logger::info(const std::string& msg) { write("INFO", msg); }
void Logger::warn(const std::string& msg) { write("WARN", msg); }
void Logger::error(const std::string& msg) { write("ERROR", msg); }

void Logger::result(const std::string& seriesName, double dlTime, double convTime) {
    std::ostringstream oss;
    oss << std::left << std::setw(45) << seriesName
        << " | DL: " << std::fixed << std::setprecision(2) << std::setw(8) << dlTime << "s"
        << " | Conv: " << std::setw(8) << convTime << "s";
    write("RESULT", oss.str());
}

void Logger::write(const std::string& level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_logPath.empty()) return;

    try {
        // Stream persistente: niente più open/stat/close per ogni riga.
        // Il check di rotazione è throttlato (max 1 volta/secondo): il costo
        // del file_size non si ripete per ogni riga.
        auto now = std::chrono::steady_clock::now();
        if (now - s_lastRotCheck >= std::chrono::seconds(1)) {
            s_lastRotCheck = now;
            if (std::filesystem::exists(s_logPath) && std::filesystem::file_size(s_logPath) > MAX_LOG_SIZE) {
                s_logStream.close();
                auto oldPath = s_logPath;
                oldPath += ".old";
                std::filesystem::rename(s_logPath, oldPath);
                s_logStream.open(s_logPath, std::ios::app);
            }
        }

        if (!s_logStream.is_open()) {
            s_logStream.open(s_logPath, std::ios::app);
        }
        if (s_logStream.is_open()) {
            auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            s_logStream << "[" << std::put_time(std::localtime(&tt), "%Y-%m-%d %H:%M:%S") << "] "
                        << "[" << std::setw(6) << std::left << level << "] "
                        << msg << std::endl;
        }
    } catch (...) {} // intenzionale: il logger non deve mai lanciare (rete di sicurezza)
}

} // namespace Core
