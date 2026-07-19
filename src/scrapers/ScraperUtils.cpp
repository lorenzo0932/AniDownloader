#include "scrapers/ScraperUtils.hpp"
#include "core/Logger.hpp"
#include <filesystem>
#include <regex>
#include <algorithm>
#include <iomanip>
#include <cstdlib>
#include <thread>
#include <cpr/cpr.h>

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
    #ifdef _WIN32
        const char* home = std::getenv("USERPROFILE");
    #else
        const char* home = std::getenv("HOME");
    #endif
        return home ? std::string(home) + path.substr(1) : path;
    }

    // Helper statico per proteggere i percorsi per la shell
    std::string ScraperUtils::Q(const std::string& path) {
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

    FILE* ScraperUtils::popenCompat(const std::string& cmd, const char* mode) {
    #ifdef _WIN32
        return _popen(cmd.c_str(), mode);
    #else
        return popen(cmd.c_str(), mode);
    #endif
    }

    int ScraperUtils::pcloseCompat(FILE* fp) {
    #ifdef _WIN32
        return _pclose(fp);
    #else
        return pclose(fp);
    #endif
    }

    std::string ScraperUtils::platformUserAgent() {
    #ifdef _WIN32
        return "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";
    #elif defined(__APPLE__)
        return "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";
    #else
        return "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";
    #endif
    }

    EpisodeInfo ScraperUtils::getHighestEpisodeFile(const std::string& seriesPath) {
        std::string fullPath = expandTilde(seriesPath);
        if (!fs::exists(fullPath)) return {0, ""};
        
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
        return {maxEp, maxEpPath.string()};
    }

    int ScraperUtils::getNextEpisodeNum(const std::string& seriesPath) {
        auto ep = getHighestEpisodeFile(seriesPath);
        if (ep.number == 0) return 1;

        // Se l'ultimo file locale è corrotto fisicamente, lo segnaliamo allo scraper
        // ritornando il numero corrente (invece di number + 1) in modo che cerchi l'URL online per riscaricarlo.
        if (!ep.path.empty()) {
            std::string qPath = Q(ep.path);
            std::string cmd = "ffprobe -v error -select_streams v:0 -show_entries stream=codec_name -of default=noprint_wrappers=1:nokey=1 " + qPath + DEVNULL();
            if (std::system(cmd.c_str()) != 0) {
                Core::Logger::warn("Rilevato file corrotto durante lo scanning: " 
                          + fs::path(ep.path).filename().string() + ". Verrà pianificato il riscaricamento.");
                return ep.number; 
            }
        }
        
        return ep.number + 1;
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

    cpr::Response ScraperUtils::httpGetWithRetry(const cpr::Url& url, const cpr::Header& headers,
                                                  int maxRetries, int retryDelayMs) {
        cpr::Response lastResponse;
        for (int attempt = 1; attempt <= maxRetries; ++attempt) {
            try {
                cpr::Response r = cpr::Get(
                    url, headers,
                    cpr::VerifySsl{false},
                    cpr::Timeout{10000}
                );
                bool retryableStatus = (r.status_code == 429 || r.status_code == 500 ||
                                        r.status_code == 502 || r.status_code == 503 ||
                                        r.status_code == 504);
                if (r.status_code == 200 || !retryableStatus || attempt == maxRetries) {
                    return r;
                }
                lastResponse = r;
                Core::Logger::warn("HTTP GET fallito (status " + std::to_string(r.status_code) +
                                   "), tentativo " + std::to_string(attempt) + "/" + std::to_string(maxRetries));
            } catch (const std::exception& e) {
                lastResponse = cpr::Response{};
                lastResponse.status_code = 0;
                if (attempt == maxRetries) return lastResponse;
                Core::Logger::warn("HTTP GET eccezione: " + std::string(e.what()) +
                                   ", tentativo " + std::to_string(attempt) + "/" + std::to_string(maxRetries));
            }
            int delay = retryDelayMs * (1 << (attempt - 1)); // exponential backoff
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        }
        return lastResponse;
    }

    std::string ScraperUtils::fetchSeriesNameFromUrl(const std::string& url) {
        try {
            cpr::Response r = httpGetWithRetry(
                cpr::Url{url},
                cpr::Header{{"User-Agent", platformUserAgent()}}
            );
            if (r.status_code != 200) return "";

            static const std::regex titleRegex(R"(<title>(.*?)</title>)", std::regex_constants::icase);
            std::smatch m;
            if (!std::regex_search(r.text, m, titleRegex)) return "";

            std::string title = m[1].str();

            static const std::vector<std::string> prefixes = {
                "AnimeWorld - ", "AnimeUnity - ", "AnimeWorld -"
            };
            for (const auto& prefix : prefixes) {
                if (title.size() >= prefix.size() && title.compare(0, prefix.size(), prefix) == 0) {
                    title = title.substr(prefix.size());
                    break;
                }
            }

            static const std::regex suffixRegex(R"(\s+(?:Episodio|Episode)\s+.*)", std::regex_constants::icase);
            title = std::regex_replace(title, suffixRegex, "");

            auto start = title.find_first_not_of(" \t\n\r");
            auto end = title.find_last_not_of(" \t\n\r");
            if (start == std::string::npos) return "";
            return title.substr(start, end - start + 1);
        } catch (...) {
            return "";
        }
    }
}