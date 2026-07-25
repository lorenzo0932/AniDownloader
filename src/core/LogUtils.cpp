#include "core/LogUtils.hpp"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <vector>

namespace Core {

std::vector<std::string> getRecentLines(const std::string& logPath, int n) {
    std::vector<std::string> allLines;
    std::ifstream f(logPath);
    if (!f) return {};
    std::string line;
    while (std::getline(f, line))
        allLines.push_back(line);
    if (n <= 0 || static_cast<size_t>(n) >= allLines.size())
        return allLines;
    return std::vector<std::string>(allLines.end() - n, allLines.end());
}

} // namespace Core
