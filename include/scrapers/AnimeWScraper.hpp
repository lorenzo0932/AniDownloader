#pragma once

#include "scrapers/BaseScraper.hpp"
#include <string>
#include <vector>

namespace Core {

    class AnimeWScraper : public BaseScraper {
    public:
        // Implementazione obbligatoria del metodo virtuale puro
        DownloadTask planSeriesTask(const Series& series) override;

    private:
        // Struttura di supporto interna equivalente al dict {"number": ep_num, "page_url": url}
        struct EpisodeData {
            int number;
            std::string pageUrl;
        };

        // Helper per unire gli URL (equivalente a urllib.parse.urljoin)
        std::string urlJoin(const std::string& base, const std::string& relative);
    };

}