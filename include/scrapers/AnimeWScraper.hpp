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

        // ---- METADATI SERIE (pagina /play): poster HD + trama + stato/anno/generi ----
        struct SeriesMetadata {
            std::string posterUrl; // og:image (copertina HD)
            std::string trama;     // div.desc
            std::string stato;     // dt Stato
            std::string anno;      // dt Anno (solo anno, es. "2007")
            std::vector<std::string> generi; // dt Generi (link)
        };

        // Parser puro (nessuna rete): metadati dalla pagina serie.
        static SeriesMetadata parseSeriesMetadata(const std::string& html);
        // Rete: scarica la pagina serie e ne estrae i metadati.
        static SeriesMetadata fetchSeriesMetadata(const std::string& pageUrl);
    };

} // namespace Core
