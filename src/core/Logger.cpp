#include "core/Logger.hpp"
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace Core {

std::filesystem::path Logger::s_logPath;
std::mutex Logger::s_mutex;

static constexpr std::uintmax_t MAX_LOG_SIZE = 5 * 1024 * 1024; // 5 MB

void Logger::init(const std::string& logFilePath) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_logPath = logFilePath;
    try {
        std::filesystem::create_directories(s_logPath.parent_path());
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
        if (std::filesystem::exists(s_logPath) && std::filesystem::file_size(s_logPath) > MAX_LOG_SIZE) {
            auto oldPath = s_logPath;
            oldPath += ".old";
            std::filesystem::rename(s_logPath, oldPath);
        }

        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::ofstream logFile(s_logPath, std::ios::app);
        if (logFile.is_open()) {
            logFile << "[" << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << "] "
                    << "[" << std::setw(6) << std::left << level << "] "
                    << msg << std::endl;
        }
    } catch (...) {} // intenzionale: il logger non deve mai lanciare (rete di sicurezza)
}

} // namespace Core
