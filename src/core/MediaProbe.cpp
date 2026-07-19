#include "core/MediaProbe.hpp"
#include "core/ProcessUtils.hpp"
#include "core/Logger.hpp"
#include "scrapers/ScraperUtils.hpp"
#include <filesystem>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

namespace Core {
namespace MediaProbe {

struct CacheEntry {
    std::string codec;
    double duration = 0.0;
    std::uintmax_t fileSize = 0;
    fs::file_time_type mtime;
};

static std::mutex s_cacheMutex;
static std::unordered_map<std::string, CacheEntry> s_cache;

static bool isCacheValid(const std::string& path, const CacheEntry& entry) {
    std::error_code ec;
    auto currentMtime = fs::last_write_time(path, ec);
    if (ec) return false;
    auto currentSize = fs::file_size(path, ec);
    if (ec) return false;
    return entry.mtime == currentMtime && entry.fileSize == currentSize;
}

void clearCache() {
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    s_cache.clear();
}

double getVideoDuration(const std::string& filePath, std::atomic<bool>& stopSignal) {
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        auto it = s_cache.find(filePath);
        if (it != s_cache.end() && isCacheValid(filePath, it->second) && it->second.duration > 0) {
            return it->second.duration;
        }
    }

    std::string cmd = "ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "
                      + ScraperUtils::Q(filePath);
    std::string output;
    ProcessUtils::runCommand(cmd, stopSignal, [&](const std::string& line) { output += line; });
    double duration = 0.0;
    try { duration = std::stod(output); } catch (...) {}

    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        auto& entry = s_cache[filePath];
        entry.duration = duration;
        std::error_code ec;
        entry.fileSize = fs::file_size(filePath, ec);
        entry.mtime = fs::last_write_time(filePath, ec);
    }

    return duration;
}

std::string getVideoCodec(const std::string& filePath, std::atomic<bool>& stopSignal) {
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        auto it = s_cache.find(filePath);
        if (it != s_cache.end() && isCacheValid(filePath, it->second) && !it->second.codec.empty()) {
            return it->second.codec;
        }
    }

    std::string cmd = "ffprobe -v error -select_streams v:0 -show_entries stream=codec_name "
                      "-of default=noprint_wrappers=1:nokey=1 " + ScraperUtils::Q(filePath);
    std::string output;
    ProcessUtils::runCommand(cmd, stopSignal, [&](const std::string& line) { output += line; });

    output.erase(std::remove(output.begin(), output.end(), '\n'), output.end());
    output.erase(std::remove(output.begin(), output.end(), '\r'), output.end());

    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        auto& entry = s_cache[filePath];
        entry.codec = output;
        std::error_code ec;
        entry.fileSize = fs::file_size(filePath, ec);
        entry.mtime = fs::last_write_time(filePath, ec);
    }

    return output;
}

bool isMediaFileHealthy(const std::string& filePath, std::atomic<bool>& stopSignal) {
    if (!fs::exists(filePath)) return false;
    if (fs::file_size(filePath) < 1048576) return false;

    std::string verifyCmd = "ffmpeg -v error -xerror -i " + ScraperUtils::Q(filePath) + " -c copy -f null -";
    int status = ProcessUtils::runCommand(verifyCmd, stopSignal, nullptr);
    return (status == 0);
}

bool verifyIntegrity(const std::string& filePath, double expectedDuration, std::atomic<bool>& stopSignal) {
    double actualDuration = getVideoDuration(filePath, stopSignal);

    double tolerance = std::max(10.0, expectedDuration * 0.02);

    if (std::abs(actualDuration - expectedDuration) > tolerance) {
        Logger::error(filePath + ": Verifica fallita: file troncato. Durata attesa: " +
                 ProcessUtils::formatFloat(expectedDuration) + "s, Durata reale: " +
                 ProcessUtils::formatFloat(actualDuration) + "s");
        return false;
    }

    std::string verifyCmd = "ffmpeg -v error -xerror -i " + ScraperUtils::Q(filePath) + " -c copy -f null -";
    int status = ProcessUtils::runCommand(verifyCmd, stopSignal, nullptr);

    if (status != 0) {
        Logger::error(filePath + ": Verifica fallita: rilevata corruzione del container o pacchetti invalidi (exit status " + std::to_string(status) + ")");
        return false;
    }

    return true;
}

} // namespace MediaProbe
} // namespace Core
