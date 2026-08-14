#include "scrapers/AnimeUScraper.hpp"
#include "scrapers/ScraperUtils.hpp"
#include "core/Logger.hpp"
#include <cpr/cpr.h>
#include <regex>
#include <algorithm>
#include <thread>
#include <chrono>

namespace Core {

// Risolve un URL relativo (href) contro l'URL della pagina da cui è stato estratto.
static std::string resolveUrl(const std::string& base, const std::string& rel) {
    if (rel.find("http") == 0) return rel;
    auto schemeEnd = base.find("://");
    if (schemeEnd == std::string::npos) return rel;
    auto hostStart = schemeEnd + 3;
    auto hostEnd = base.find('/', hostStart);
    std::string origin = (hostEnd == std::string::npos) ? base : base.substr(0, hostEnd);
    if (!rel.empty() && rel[0] == '/') return origin + rel;
    auto lastSlash = base.rfind('/');
    if (lastSlash == std::string::npos || lastSlash < hostStart) return origin + "/" + rel;
    return base.substr(0, lastSlash + 1) + rel;
}

std::vector<DownloadTask> AnimeUScraper::planSeriesTask(const Series& series, std::atomic<bool>&,
    ScraperProgressCb progressCb)
{
    std::vector<DownloadTask> results;

    auto episodesMap = ScraperUtils::scanEpisodesMap(series.path);
    int nextNeeded = ScraperUtils::computeNextNeeded(series.path, series.lastDownloadedEpisode, episodesMap);

    try {
        cpr::Session session;
        session.SetVerifySsl(cpr::VerifySsl{false});
        session.SetHeader({{"User-Agent", ScraperUtils::platformUserAgent()}});

        cpr::Response seriesPageResp = ScraperUtils::httpGetWithRetry(
            cpr::Url{series.seriesPageUrl},
            {{"User-Agent", ScraperUtils::platformUserAgent()}},
            3, 2000
        );
        if (seriesPageResp.status_code != 200) {
            Logger::warn(series.name + ": fetch pagina serie fallito (status " +
                         std::to_string(seriesPageResp.status_code) + ")");
            return results;
        }
        if (seriesPageResp.text.empty()) {
            Logger::warn(series.name + ": HTML pagina serie vuoto");
            return results;
        }

        std::regex epRegex(R"raw(class="episode-item"[^>]*href="([^"]+)"[^>]*>.*?(\d+))raw");

        auto words_begin = std::sregex_iterator(seriesPageResp.text.begin(), seriesPageResp.text.end(), epRegex);
        auto words_end = std::sregex_iterator();

        struct Ep { int n; std::string u; };
        std::vector<Ep> eps;

        for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
            std::smatch match = *i;
            eps.push_back({std::stoi(match[2].str()), match[1].str()});
        }

        if (eps.empty()) {
            Logger::warn(series.name + ": HTML scaricato (" + std::to_string(seriesPageResp.text.size()) +
                         " byte) ma 0 episodi: markup cambiato?");
            return results;
        }
        std::sort(eps.begin(), eps.end(), [](const Ep& a, const Ep& b) { return a.n < b.n; });

        size_t totalToCheck = 0;
        for (const auto& ep : eps) {
            int local = series.continueSeries ? (ep.n + series.passedEpisodes) : ep.n;
            if (local >= nextNeeded) totalToCheck++;
        }
        if (progressCb) progressCb("Analisi: " + std::to_string(totalToCheck) + " episodi da verificare");

        size_t checked = 0;
        for (const auto& ep : eps) {
            int local = series.continueSeries ? (ep.n + series.passedEpisodes) : ep.n;
            if (local >= nextNeeded) {
                checked++;
                if (progressCb) progressCb("Analisi: verifica episodio " + std::to_string(checked) + "/" + std::to_string(totalToCheck) + "...");
                cpr::Response epPage = ScraperUtils::httpGetWithRetry(
                    cpr::Url{resolveUrl(series.seriesPageUrl, ep.u)},
                    {{"User-Agent", ScraperUtils::platformUserAgent()}},
                    3, 2000
                );
                if (epPage.status_code != 200) {
                    Logger::warn(series.name + ": Ep " + std::to_string(ep.n) +
                                 " - fetch pagina episodio fallito (status " +
                                 std::to_string(epPage.status_code) + ")");
                    continue;
                }

                std::regex iframeRegex(R"raw(<iframe[^>]*id="embed"[^>]*src="([^"]+)")raw");
                std::smatch iframeMatch;
                if (!std::regex_search(epPage.text, iframeMatch, iframeRegex)) {
                    Logger::warn(series.name + ": Ep " + std::to_string(ep.n) + " - iframe non trovato");
                    continue;
                }

                std::string iframeUrl = iframeMatch[1].str();

                cpr::Response iframePage = ScraperUtils::httpGetWithRetry(
                    cpr::Url{iframeUrl},
                    {{"User-Agent", ScraperUtils::platformUserAgent()}},
                    3, 2000
                );
                if (iframePage.status_code != 200) {
                    Logger::warn(series.name + ": Ep " + std::to_string(ep.n) +
                                 " - fetch iframe fallito (status " +
                                 std::to_string(iframePage.status_code) + ")");
                    continue;
                }

                std::regex dlRegex(R"raw(window\.downloadUrl\s*=\s*"([^"]+)")raw");
                std::smatch dlMatch;
                if (!std::regex_search(iframePage.text, dlMatch, dlRegex)) {
                    Logger::warn(series.name + ": Ep " + std::to_string(ep.n) + " - download URL non trovato");
                    continue;
                }

                DownloadTask task;
                task.videoUrl = dlMatch[1].str();
                task.episodeNumber = local;
                task.shouldProcess = true;
                task.fileName = series.name + "_Ep_" + std::to_string(local) + ".mp4";
                results.push_back(task);
            }
        }
    } catch (const std::exception& e) {
        DownloadTask err;
        err.errorMessage = e.what();
        results.push_back(err);
    }

    return results;
}

}
