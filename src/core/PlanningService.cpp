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

    std::vector<DownloadTask> PlanningService::planSingleSeries(const Series& series,
        BaseScraper::ScraperProgressCb progressCb)
    {
        if (series.service.empty()) {
            DownloadTask err;
            err.shouldProcess = false;
            err.errorMessage = "Errore: Campo 'service' mancante per la serie: " + series.name;
            return {err};
        }

        auto scraper = getScraperInstance(series.service);
        if (!scraper) {
            DownloadTask err;
            err.shouldProcess = false;
            err.errorMessage = "Scraper non trovato per il servizio: " + series.service;
            return {err};
        }

        std::atomic<bool> stop{false};
        std::vector<DownloadTask> tasks;
        try {
            tasks = scraper->planSeriesTask(series, stop, progressCb);
        } catch (const std::exception& e) {
            DownloadTask err;
            err.shouldProcess = false;
            err.errorMessage = "Eccezione durante lo scraping: " + std::string(e.what());
            return {err};
        }

        // --- FASE 2: COERENZA CODEC LOCALE (SOLO SE SIAMO IN PARI) ---
        if (tasks.empty()) {
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
                        DownloadTask conv;
                        conv.shouldProcess = true;
                        conv.episodeNumber = ep.number;
                        conv.fileName = fs::path(ep.path).filename().string();
                        conv.videoUrl = "";

                        Core::Logger::info("Serie " + series.name + " in pari. Pianifico conversione locale H265 per Ep. " + std::to_string(ep.number));
                        tasks.push_back(conv);
                    }
                }
            }
        }

        return tasks;
    }

}