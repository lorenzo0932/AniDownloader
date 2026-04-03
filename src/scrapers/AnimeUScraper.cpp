#include "scrapers/AnimeUScraper.hpp"
#include "scrapers/ScraperUtils.hpp"
#include <cpr/cpr.h>
#include <regex>
#include <iostream>
#include <algorithm>

namespace Core {

DownloadTask AnimeUScraper::planSeriesTask(const Series& series) {
    DownloadTask task;
    task.shouldProcess = false;

    try {
        cpr::Response r = cpr::Get(cpr::Url{series.seriesPageUrl}, 
                                 cpr::Header{{"User-Agent", "Mozilla/5.0"}},
                                 cpr::VerifySsl{false});

        if (r.status_code != 200) return task;

        // USARE R"raw(...)raw" PER EVITARE ERRORI DI SINTASSI NELLE REGEX
        std::regex epRegex(R"raw(class="episode-item"[^>]*href="([^"]+)"[^>]*>.*?(\d+))raw");
        
        auto words_begin = std::sregex_iterator(r.text.begin(), r.text.end(), epRegex);
        auto words_end = std::sregex_iterator();

        struct Ep { int n; std::string u; };
        std::vector<Ep> eps;

        for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
            std::smatch match = *i;
            eps.push_back({std::stoi(match[2].str()), match[1].str()});
        }

        if (eps.empty()) return task;

        std::sort(eps.begin(), eps.end(), [](const Ep& a, const Ep& b) { return a.n < b.n; });

        int next = ScraperUtils::getNextEpisodeNum(series.path);
        for (const auto& ep : eps) {
            int local = series.continueSeries ? (ep.n + series.passedEpisodes) : ep.n;
            if (local >= next) {
                // IL LINK TROVATO NON È IL VIDEO DIRETTO.
                // DOBBIAMO ANDARE SULLA PAGINA DELL'EPISODIO, TROVARE L'IFRAME, CARICARE L'IFRAME 
                // E ESTRARRE 'window.downloadUrl'.
                
                cpr::Response epPage = cpr::Get(cpr::Url{ep.u}, 
                                             cpr::Header{{"User-Agent", "Mozilla/5.0"}},
                                             cpr::VerifySsl{false});
                
                if (epPage.status_code != 200) continue;

                // Cerca l'iframe con id="embed"
                std::regex iframeRegex(R"raw(<iframe[^>]*id="embed"[^>]*src="([^"]+)")raw");
                std::smatch iframeMatch;
                if (!std::regex_search(epPage.text, iframeMatch, iframeRegex)) continue;

                std::string iframeUrl = iframeMatch[1].str();
                
                // Ora carichiamo l'URL dell'iframe
                cpr::Response iframePage = cpr::Get(cpr::Url{iframeUrl}, 
                                                 cpr::Header{{"User-Agent", "Mozilla/5.0"}},
                                                 cpr::VerifySsl{false});
                
                if (iframePage.status_code != 200) continue;

                // Cerchiamo window.downloadUrl = "..."
                std::regex dlRegex(R"raw(window\.downloadUrl\s*=\s*"([^"]+)")raw");
                std::smatch dlMatch;
                if (!std::regex_search(iframePage.text, dlMatch, dlRegex)) continue;

                task.videoUrl = dlMatch[1].str();
                task.episodeNumber = local;
                task.shouldProcess = true;
                task.fileName = series.name + "_Ep_" + std::to_string(local) + ".mp4";
                break;
            }
        }
    } catch (const std::exception& e) {
        task.errorMessage = e.what();
    }

    return task;
}

}