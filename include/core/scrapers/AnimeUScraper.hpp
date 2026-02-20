#pragma once

#include "core/scrapers/BaseScraper.hpp"
#include <string>

namespace Core {

    class AnimeUScraper : public BaseScraper {
    public:
        DownloadTask planSeriesTask(const Series& series) override;

    private:
        // Helper per simulare il comportamento di Selenium senza browser
        std::string extractDownloadUrlFromIframe(const std::string& iframeUrl);
    };

}