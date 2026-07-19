#include "scrapers/ScraperUtils.hpp"
#include <filesystem>
#include <regex>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <thread>

namespace fs = std::filesystem;

namespace Core {

    // Definizione dei membri statici di ScraperSemaphoreGuard
    std::mutex ScraperSemaphoreGuard::s_mutex;
    std::condition_variable ScraperSemaphoreGuard::s_cv;
    int ScraperSemaphoreGuard::s_activeSessions = 0;

    int ScraperSemaphoreGuard::getMaxConcurrent() {
        unsigned int threads = std::thread::hardware_concurrency();
        if (threads == 0) return 2;
        if (threads < 6)   return 1; // Macchine low-end: limitiamo a 1 per non saturare la RAM
        if (threads <= 12) return 3; // PC standard consumer (6 core / 12 thread): fino a 3 Chrome paralleli
        if (threads <= 24) return 4; // PC high-end (8-12 core): fino a 4 Chrome paralleli
        return 6;                    // Workstation/Server (> 12 core): fino a 6 Chrome paralleli
    }

    ScraperSemaphoreGuard::ScraperSemaphoreGuard() {
        std::unique_lock<std::mutex> lock(s_mutex);
        s_cv.wait(lock, [&]() { return s_activeSessions < getMaxConcurrent(); });
        s_activeSessions++;
    }

    ScraperSemaphoreGuard::~ScraperSemaphoreGuard() {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_activeSessions--;
        s_cv.notify_all();
    }

    std::string ScraperUtils::expandTilde(const std::string& path) {
        if (path.empty() || path[0] != '~') return path;
        const char* home = std::getenv("HOME");
        return home ? std::string(home) + path.substr(1) : path;
    }

    // Helper statico per proteggere i percorsi per la shell
    static std::string escapePath(const std::string& path) {
#ifdef _WIN32
        std::string escaped = "\"";
        for (char c : path) {
            if (c == '"') escaped += "\\\"";
            else escaped += c;
        }
        escaped += "\"";
        return escaped;
#else
        std::string escaped = "'";
        for (char c : path) {
            if (c == '\'') escaped += "'\\''";
            else escaped += c;
        }
        escaped += "'";
        return escaped;
#endif
    }

    int ScraperUtils::getNextEpisodeNum(const std::string& seriesPath) {
        std::string fullPath = expandTilde(seriesPath);
        if (!fs::exists(fullPath)) return 1;
        
        int maxEp = 0;
        fs::path maxEpPath;
        static const std::regex epRegex(R"raw([._\s-]Ep[._\s-]?(\d+))raw", std::regex_constants::icase);
        try {
            for (const auto& entry : fs::directory_iterator(fullPath)) {
                if (!entry.is_regular_file() || fs::file_size(entry.path()) < 1000000) continue;
                std::string filename = entry.path().filename().string();
                std::smatch match;
                if (std::regex_search(filename, match, epRegex)) {
                    int num = std::stoi(match[1].str());
                    if (num < 2000 && num > maxEp) {
                        maxEp = num;
                        maxEpPath = entry.path();
                    }
                }
            }
        } catch (...) {}
        
        // Se l'ultimo file locale è corrotto fisicamente, lo segnaliamo allo scraper
        // ritornando maxEp (invece di maxEp + 1) in modo che cerchi l'URL online per riscaricarlo.
        if (maxEp > 0 && !maxEpPath.empty()) {
            std::string qPath = escapePath(maxEpPath.string());
            // Controllo ffprobe ultra-veloce (millisecondi) per verificare che il container sia integro
            std::string cmd = "ffprobe -v error -select_streams v:0 -show_entries stream=codec_name -of default=noprint_wrappers=1:nokey=1 " + qPath + " > /dev/null 2>&1";
            if (std::system(cmd.c_str()) != 0) {
                std::cout << "[AUTOCURA] Rilevato file corrotto durante lo scanning: " 
                          << maxEpPath.filename().string() << ". Verrà pianificato il riscaricamento." << std::endl;
                return maxEp; 
            }
        }
        
        return maxEp + 1;
    }

    std::string ScraperUtils::generateFilename(const std::string& downloadUrl, int epNum) {
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