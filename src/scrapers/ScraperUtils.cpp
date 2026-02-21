#include "scrapers/ScraperUtils.hpp"
#include <filesystem>
#include <regex>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <cstdlib>

namespace fs = std::filesystem;

namespace Core {

    std::string ScraperUtils::expandTilde(const std::string& path) {
        if (path.empty() || path[0] != '~') return path;
        const char* home = std::getenv("HOME");
        return home ? std::string(home) + path.substr(1) : path;
    }

    // FUNZIONE CHIAVE: Protegge i percorsi per la shell Linux
    // Trasforma /path/"Omae" in '/path/"Omae"'
    static std::string escapePath(const std::string& path) {
        std::string escaped = "'";
        for (char c : path) {
            if (c == '\'') escaped += "'\\''"; // Gestisce l'apice singolo se presente
            else escaped += c;
        }
        escaped += "'";
        return escaped;
    }

    int ScraperUtils::getNextEpisodeNum(const std::string& seriesPath) {
        std::string fullPath = expandTilde(seriesPath);
        if (!fs::exists(fullPath)) return 1;
        int maxEp = 0;
        static const std::regex epRegex(R"raw([._\s-]Ep[._\s-]?(\d+))raw", std::regex_constants::icase);
        try {
            for (const auto& entry : fs::directory_iterator(fullPath)) {
                if (!entry.is_regular_file() || fs::file_size(entry.path()) < 1000000) continue;
                std::string filename = entry.path().filename().string();
                std::smatch match;
                if (std::regex_search(filename, match, epRegex)) {
                    int num = std::stoi(match[1].str());
                    if (num < 2000 && num > maxEp) maxEp = num;
                }
            }
        } catch (...) {}
        return maxEp + 1;
    }

    std::string ScraperUtils::generateFilename(const std::string& downloadUrl, const std::string& seriesName, int epNum) {
        std::string urlFile = downloadUrl.substr(downloadUrl.find_last_of("/") + 1);
        if (urlFile.find("?") != std::string::npos) urlFile = urlFile.substr(0, urlFile.find("?"));

        std::string root = "";
        std::smatch m;
        static const std::regex rootRegex(R"raw((.*?)_Ep_)raw", std::regex_constants::icase);
        if (std::regex_search(urlFile, m, rootRegex)) root = m[1].str();
        else root = fs::path(urlFile).stem().string();

        std::ostringstream ss;
        ss << std::setw(2) << std::setfill('0') << epNum;
        std::string epStr = ss.str();

        static const std::regex suffixRegex(R"raw((_Ep_.*))raw", std::regex_constants::icase);
        if (std::regex_search(urlFile, m, suffixRegex)) {
            static const std::regex numRegex(R"raw(\d+)raw");
            return root + std::regex_replace(m[1].str(), numRegex, epStr, std::regex_constants::format_first_only);
        }
        return root + "_Ep_" + epStr + ".mp4";
    }
}