#pragma once

#include "scrapers/BaseScraper.hpp"
#include <string>
#include <vector>

namespace Core {

    class AnimeWScraper : public BaseScraper {
    public:
        std::vector<DownloadTask> planSeriesTask(const Series& series) override;
    };

}