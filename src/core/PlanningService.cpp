#include "core/PlanningService.hpp"
#include "config/AppConfigManager.hpp"
#include "core/Logger.hpp"
#include "core/MediaProbe.hpp"
#include "scrapers/AnimeUScraper.hpp"
#include "scrapers/AnimeWScraper.hpp"
#include "scrapers/ScraperUtils.hpp"
#include <algorithm>
#include <filesystem>
#include <format>

namespace fs = std::filesystem;

namespace Core {

    std::unique_ptr<BaseScraper>
    PlanningService::getScraperInstance(const std::string& serviceName) {
        if (serviceName == "animeW_scraper") {
            return std::make_unique<AnimeWScraper>();
        } else if (serviceName == "animeU_scraper") {
            return std::make_unique<AnimeUScraper>();
        }
        return nullptr;
    }

    std::vector<DownloadTask>
    PlanningService::planSingleSeries(const Series& series,
                                      std::function<void(const std::string&)> progressCb) {
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
                    // Unica fonte di verità per la salute del file (stessa di
                    // MediaProcessor::processTask): la decodifica completa. Un
                    // file corrotto non deve generare un task di conversione
                    // bogus che poi fallisce con "File sorgente mancante".
                    std::atomic<bool> dummyStop{false};
                    if (MediaProbe::isMediaFileHealthy(ep.path, dummyStop)) {
                        std::string codec = MediaProbe::getVideoCodec(ep.path, dummyStop);

                        if (codec != "hevc" && codec != "h265") {
                            DownloadTask conv;
                            conv.shouldProcess = true;
                            conv.episodeNumber = ep.number;
                            conv.fileName = fs::path(ep.path).filename().string();
                            conv.videoUrl = "";

                            Core::Logger::info(
                                std::format("Serie {} in pari. Pianifico conversione locale H265 "
                                            "per Ep. {}",
                                            series.name, ep.number));
                            tasks.push_back(conv);
                        }
                    }
                }
            }
        }

        return tasks;
    }

} // namespace Core