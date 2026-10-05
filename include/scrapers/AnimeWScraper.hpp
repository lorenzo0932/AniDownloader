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

        // ---- SCOPRI: card dalla pagina filtro/ricerca AnimeWorld ----
        struct DiscoverItem {
            std::string titolo;
            std::string pageUrl;   // relativo, es. /play/slug.ID
            std::string posterUrl; // URL diretto locandina (img.animeworld.ac)
            std::string tooltipId; // id numerico di api/tooltip/NNNN
        };

        struct DiscoverPage {
            std::vector<DiscoverItem> items;
            int maxPage = 1;
        };

        // Dettaglio lazy dal tooltip (api/tooltip/NNNN).
        struct DiscoverDetail {
            std::string trama;
            std::string voto;
            std::string stato;
            std::string dataUscita;
            std::string nomiAlternativi;
            std::vector<std::string> generi;
        };

        // Parser puri (nessuna rete): card + maxPage da paginationMaxPage.
        static DiscoverPage parseDiscoverPage(const std::string& html);
        static DiscoverDetail parseTooltip(const std::string& html);

        // Rete: scarica una pagina Scopri (path "/filter" oppure "/search" con
        // parametri già validati dal chiamante; l'encoding è a carico di cpr)
        // e il tooltip di una card.
        static DiscoverPage fetchDiscoverPage(
            const std::string& path,
            const std::vector<std::pair<std::string, std::string>>& params);
        static DiscoverDetail fetchTooltip(const std::string& tooltipId);
    };

} // namespace Core
