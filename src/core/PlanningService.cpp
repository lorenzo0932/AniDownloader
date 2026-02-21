#include "core/PlanningService.hpp"
#include "scrapers/AnimeWScraper.hpp"
#include "scrapers/AnimeUScraper.hpp"
#include <iostream>

namespace Core {

    std::unique_ptr<BaseScraper> PlanningService::getScraperInstance(const std::string& serviceName) {
        // Mappatura identica a SCRAPER_CLASS_MAP di Python
        if (serviceName == "animeW_scraper") {
            return std::make_unique<AnimeWScraper>();
        } 
        else if (serviceName == "animeU_scraper") {
            return std::make_unique<AnimeUScraper>();
        }
        
        // Se aggiungi nuovi scraper in futuro, aggiungi qui un else if
        
        return nullptr; 
    }

    DownloadTask PlanningService::planSingleSeries(const Series& series) {
        DownloadTask task;
        
        // Verifica preliminare
        if (series.service.empty()) {
            task.shouldProcess = false;
            task.errorMessage = "Errore: Campo 'service' mancante per la serie: " + series.name;
            return task;
        }

        // Istanziamo lo scraper
        auto scraper = getScraperInstance(series.service);
        
        if (!scraper) {
            task.shouldProcess = false;
            task.errorMessage = "Scraper non trovato per il servizio: " + series.service;
            return task;
        }

        try {
            // Eseguiamo la logica di scraping (reale o simulata tramite CPR)
            return scraper->planSeriesTask(series);
        } catch (const std::exception& e) {
            task.shouldProcess = false;
            task.errorMessage = "Eccezione durante lo scraping: " + std::string(e.what());
            return task;
        }
    }

}