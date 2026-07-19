#include "core/PlanningService.hpp"
#include "scrapers/AnimeWScraper.hpp"
#include "scrapers/AnimeUScraper.hpp"
#include "config/AppConfigManager.hpp"
#include "scrapers/ScraperUtils.hpp"
#include <iostream>
#include <filesystem>
#include <regex>
#include <algorithm>
#include <cstdlib>
#include <cstdio>

namespace fs = std::filesystem;

namespace Core {

    struct FileState {
        bool exists = false;
        bool healthy = false;
        std::string codec;
    };

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

    static FileState checkFileState(const std::string& filePath) {
        FileState state;
        if (!fs::exists(filePath)) return state;
        state.exists = true;
        
        if (fs::file_size(filePath) < 1048576) {
            state.healthy = false;
            return state;
        }

        std::string qPath = escapePath(filePath);
        std::string probeCmd = "ffprobe -v error -select_streams v:0 -show_entries stream=codec_name -of default=noprint_wrappers=1:nokey=1 " + qPath;
        
        std::string codec;
#ifdef _WIN32
        auto popen_compat = _popen;
        auto pclose_compat = _pclose;
#else
        auto popen_compat = popen;
        auto pclose_compat = pclose;
#endif

        FILE* rawPipe = popen_compat((probeCmd + " 2>/dev/null").c_str(), "r");
        if (rawPipe) {
            std::unique_ptr<FILE, decltype(pclose_compat)> pipe(rawPipe, pclose_compat);
            char buffer[128];
            if (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) {
                codec = buffer;
            }
            pipe.release();
            int status = pclose_compat(rawPipe);
            state.healthy = (status == 0); 
        } else {
            state.healthy = false;
        }

        if (state.healthy) {
            codec.erase(std::remove(codec.begin(), codec.end(), '\n'), codec.end());
            codec.erase(std::remove(codec.begin(), codec.end(), '\r'), codec.end());
            state.codec = codec;
        }

        return state;
    }

    static std::pair<int, fs::path> getHighestEpisodeFile(const std::string& seriesPath) {
        std::string fullPath = ScraperUtils::expandTilde(seriesPath);
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
        return {maxEp, maxEpPath};
    }

    std::unique_ptr<BaseScraper> PlanningService::getScraperInstance(const std::string& serviceName) {
        if (serviceName == "animeW_scraper") {
            return std::make_unique<AnimeWScraper>();
        } 
        else if (serviceName == "animeU_scraper") {
            return std::make_unique<AnimeUScraper>();
        }
        return nullptr; 
    }

    DownloadTask PlanningService::planSingleSeries(const Series& series) {
        DownloadTask task;
        
        if (series.service.empty()) {
            task.shouldProcess = false;
            task.errorMessage = "Errore: Campo 'service' mancante per la serie: " + series.name;
            return task;
        }

        // --- FASE 1: AVVIA LO SCRAPER ONLINE (PRIORITÀ MASSIMA) ---
        // Controlliamo prima se sul sito web ci sono nuovi episodi o se dobbiamo riparare un file corrotto.
        auto scraper = getScraperInstance(series.service);
        if (!scraper) {
            task.shouldProcess = false;
            task.errorMessage = "Scraper non trovato per il servizio: " + series.service;
            return task;
        }

        try {
            task = scraper->planSeriesTask(series);
        } catch (const std::exception& e) {
            task.shouldProcess = false;
            task.errorMessage = "Eccezione durante lo scraping: " + std::string(e.what());
            return task;
        }

        // --- FASE 2: COERENZA CODEC LOCALE (SOLO SE SIAMO IN PARI) ---
        // Se lo scraper ha detto che la serie è "già aggiornata" (shouldProcess == false),
        // allora e solo allora verifichiamo se l'ultimo file sano locale necessita di essere convertito in H.265.
        if (!task.shouldProcess && task.errorMessage.empty()) {
            auto [maxEp, maxEpPath] = getHighestEpisodeFile(series.path);
            
            if (maxEp > 0 && !maxEpPath.empty()) {
                Config::AppConfigManager config;
                bool requireH265 = config.get<bool>("convert_to_h265", true);
                
                if (requireH265) {
                    // Controllo ffprobe ultra-veloce (millisecondi)
                    FileState state = checkFileState(maxEpPath.string());
                    
                    // Se il file è sano ma in formato AVC (H264), forziamo la conversione locale bypassando Aria2
                    if (state.healthy && state.codec != "hevc" && state.codec != "h265" && !state.codec.empty()) {
                        task.shouldProcess = true;
                        task.episodeNumber = maxEp;
                        task.fileName = maxEpPath.filename().string();
                        task.videoUrl = ""; // Vuoto: dice a MediaProcessor di saltare il download ed avviare solo la conversione
                        
                        std::cout << "[PLANNER] Serie " << series.name << " in pari. Pianifico conversione locale H265 per Ep. " << maxEp << std::endl;
                    }
                }
            }
        }

        return task;
    }

}