#pragma once

#include "scrapers/BaseScraper.hpp"
#include <string>
#include <vector>

namespace Core {

    class AnimeWScraper : public BaseScraper {
    public:
        DownloadTask planSeriesTask(const Series& series) override;
    };

}