#include "core/PlanningService.hpp"
#include "scrapers/AnimeWScraper.hpp"
#include "scrapers/AnimeUScraper.hpp"
#include "config/AppConfigManager.hpp"
#include "scrapers/ScraperUtils.hpp"
#include "core/MediaProbe.hpp"
#include "core/Logger.hpp"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace Core {

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
        if (!task.shouldProcess && task.errorMessage.empty()) {
            auto ep = ScraperUtils::getHighestEpisodeFile(series.path);
            
            if (ep.number > 0 && !ep.path.empty()) {
                Config::AppConfigManager config;
                bool requireH265 = config.get<bool>("convert_to_h265", true);
                
                if (requireH265) {
                    bool healthy = false;
                    std::string codec;
                    
                    if (fs::exists(ep.path) && fs::file_size(ep.path) >= 1048576) {
                        std::atomic<bool> dummyStop{false};
                        codec = MediaProbe::getVideoCodec(ep.path, dummyStop);
                        healthy = !codec.empty();
                    }
                    
                    if (healthy && codec != "hevc" && codec != "h265") {
                        task.shouldProcess = true;
                        task.episodeNumber = ep.number;
                        task.fileName = fs::path(ep.path).filename().string();
                        task.videoUrl = "";
                        
                        Core::Logger::info("Serie " + series.name + " in pari. Pianifico conversione locale H265 per Ep. " + std::to_string(ep.number));
                    }
                }
            }
        }

        return task;
    }

}