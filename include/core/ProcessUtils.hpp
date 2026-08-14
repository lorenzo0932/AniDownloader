#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>

namespace Core {
    namespace ProcessUtils {

        int runCommand(const std::string& cmd, std::atomic<bool>& stopSignal,
                       std::function<void(const std::string&)> onLineRead = nullptr);

        double getRamUsagePercent();

        double parseProgressUs(const std::filesystem::path& progressFile);

        std::string formatFloat(double value, int precision = 6);

    } // namespace ProcessUtils
} // namespace Core
