#pragma once

#include "core/Series.hpp"
#include "core/DownloadTask.hpp"
#include <memory>
#include <string>
#include <functional>

namespace Core {

    // Forward declaration: getScraperInstance ritorna un unique_ptr; il tipo
    // completo serve solo nel .cpp (che include gli scraper reali).
    class BaseScraper;

    class PlanningService {
    public:
        static std::unique_ptr<BaseScraper> getScraperInstance(const std::string& serviceName);

        // La callback di progresso è il tipo di BaseScraper::ScraperProgressCb
        // (= std::function<void(const std::string&)>): qui espanso per non
        // dipendere dal tipo annidato dello scraper (direzione core ← scrapers).
        static std::vector<DownloadTask> planSingleSeries(const Series& series,
            std::function<void(const std::string&)> progressCb = nullptr);
    };

}