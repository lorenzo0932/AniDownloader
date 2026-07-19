#pragma once

#include "scrapers/BaseScraper.hpp"
#include <string>

namespace Core {

    class AnimeUScraper : public BaseScraper {
    public:
        DownloadTask planSeriesTask(const Series& series) override;
    };

}