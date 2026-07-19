#include "core/ProcessUtils.hpp"
#include "scrapers/ScraperUtils.hpp"
#ifdef _WIN32
    #include <windows.h>
#endif
#include <cstdio>
#include <memory>
#include <sstream>
#include <iomanip>
#include <locale>
#include <fstream>

namespace Core {
namespace ProcessUtils {

int runCommand(const std::string& cmd, std::atomic<bool>& stopSignal,
               std::function<void(const std::string&)> onLineRead) {
    std::string fullCmd = cmd + " 2>&1";

    FILE* rawPipe = ScraperUtils::popenCompat(fullCmd, "r");
    if (!rawPipe) return -1;

    std::unique_ptr<FILE, decltype(&ScraperUtils::pcloseCompat)> pipe(rawPipe, ScraperUtils::pcloseCompat);

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) {
        if (stopSignal) break;
        if (onLineRead) onLineRead(std::string(buffer));
    }

    pipe.release();
    return ScraperUtils::pcloseCompat(rawPipe);
}

double getRamUsagePercent() {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        return static_cast<double>(memInfo.dwMemoryLoad);
    }
    return 100.0;
#else
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open()) return 100.0;
    std::string line; long total = 1, available = 0;
    while (std::getline(meminfo, line)) {
        if (line.find("MemTotal:") == 0) std::sscanf(line.c_str(), "MemTotal: %ld", &total);
        if (line.find("MemAvailable:") == 0) std::sscanf(line.c_str(), "MemAvailable: %ld", &available);
    }
    return ((double)(total - available) / total) * 100.0;
#endif
}

double parseProgressUs(const std::filesystem::path& progressFile) {
    if (!std::filesystem::exists(progressFile)) return 0;
    std::ifstream f(progressFile);
    std::string line; long long t = 0;
    while (std::getline(f, line))
        if (line.find("out_time_us=") == 0)
            try { t = std::stoll(line.substr(12)); } catch (...) {}
    return static_cast<double>(t);
}

std::string formatFloat(double value, int precision) {
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

} // namespace ProcessUtils
} // namespace Core
