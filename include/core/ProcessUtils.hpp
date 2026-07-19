#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <filesystem>

namespace Core {
namespace ProcessUtils {

    int runCommand(const std::string& cmd, std::atomic<bool>& stopSignal,
                   std::function<void(const std::string&)> onLineRead = nullptr);

    double getRamUsagePercent();

    double parseProgressUs(const std::filesystem::path& progressFile);

    std::string formatFloat(double value, int precision = 6);

} // namespace ProcessUtils
} // namespace Core
