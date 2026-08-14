#pragma once

#include "scrapers/BaseScraper.hpp"
#include "scrapers/ScraperUtils.hpp"
#include <atomic>
#include <string>
#include <vector>

namespace Core {

    class AnimeUScraper : public BaseScraper {
      public:
        std::vector<DownloadTask> planSeriesTask(const Series& series,
                                                 std::atomic<bool>& stopSignal,
                                                 ScraperProgressCb progressCb = nullptr) override;

        // Parser puri (nessuna rete): testabili offline con fixture HTML.
        // Estrae gli episodi (numero + href) dalla pagina serie.
        static std::vector<EpisodeCandidate> parseSeriesPage(const std::string& html);
        // Estrae l'URL dell'iframe <iframe id="embed"> dalla pagina episodio.
        static std::string parseEpisodePage(const std::string& html);
        // Estrae l'URL del video (window.downloadUrl) dalla pagina dell'embed.
        static std::string parseEmbedPage(const std::string& html);
    };

} // namespace Core
