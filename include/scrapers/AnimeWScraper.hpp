#pragma once

#include "scrapers/BaseScraper.hpp"
#include "scrapers/ScraperUtils.hpp"
#include <atomic>
#include <string>
#include <vector>

namespace Core {

    class AnimeWScraper : public BaseScraper {
      public:
        std::vector<DownloadTask> planSeriesTask(const Series& series,
                                                 std::atomic<bool>& stopSignal,
                                                 ScraperProgressCb progressCb = nullptr) override;
        std::vector<EpisodeCandidate> getCandidates(const Series& series);

        // Parser puri (nessuna rete): testabili offline con fixture HTML/JSON.
        // Estrae gli episodi (numero + href) dalla pagina serie.
        static std::vector<EpisodeCandidate> parseSeriesPage(const std::string& html);
        // Estrae l'URL del video (grabber) dal body JSON dell'endpoint
        // /api/episode/info; stringa vuota se assente o se {"error": true}.
        static std::string parseEpisodeInfo(const std::string& body);
    };

} // namespace Core
