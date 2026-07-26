#pragma once

#include "core/Series.hpp"
#include "scrapers/BaseScraper.hpp"
#include <memory>
#include <string>

namespace Core {

    class PlanningService {
    public:
        /**
         * Crea lo scraper corretto in base alla stringa fornita nel JSON (campo "service").
         * Ritorna un unique_ptr per una gestione sicura della memoria.
         */
        static std::unique_ptr<BaseScraper> getScraperInstance(const std::string& serviceName);

        /**
         * Prende una serie, istanzia lo scraper adatto e produce un DownloadTask.
         * Corrisponde alla funzione plan_single_series in Python.
         */
        static std::vector<DownloadTask> planSingleSeries(const Series& series);
    };

}