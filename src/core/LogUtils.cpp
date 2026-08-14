#include "core/LogUtils.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

namespace Core {

    std::vector<std::string> getRecentLines(std::string_view logPath, int n) {
        // ifstream costruito da std::filesystem::path, a sua volta costruito
        // dalla view: nessuna copia intermedia e nessun cambio di lifetime.
        std::vector<std::string> allLines;
        std::ifstream f{std::filesystem::path(logPath)};
        if (!f)
            return {};
        std::string line;
        while (std::getline(f, line))
            allLines.push_back(line);
        if (n <= 0 || static_cast<size_t>(n) >= allLines.size())
            return allLines;
        return std::vector<std::string>(allLines.end() - n, allLines.end());
    }

} // namespace Core
