#pragma once

#include "scrapers/BaseScraper.hpp"
#include <string>

namespace Core {

    class AnimeUScraper : public BaseScraper {
    public:
        std::vector<DownloadTask> planSeriesTask(const Series& series) override;
    };

}