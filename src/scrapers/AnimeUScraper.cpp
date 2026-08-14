#include "scrapers/AnimeUScraper.hpp"
#include "core/Logger.hpp"
#include "scrapers/ScraperUtils.hpp"
#include <algorithm>
#include <chrono>
#include <cpr/cpr.h>
#include <format>
#include <regex>
#include <thread>

namespace Core {

    // Risolve un URL relativo (href) contro l'URL della pagina da cui è stato estratto.
    static std::string resolveUrl(const std::string& base, const std::string& rel) {
        if (rel.find("http") == 0)
            return rel;
        auto schemeEnd = base.find("://");
        if (schemeEnd == std::string::npos)
            return rel;
        auto hostStart = schemeEnd + 3;
        auto hostEnd = base.find('/', hostStart);
        std::string origin = (hostEnd == std::string::npos) ? base : base.substr(0, hostEnd);
        if (!rel.empty() && rel[0] == '/')
            return origin + rel;
        auto lastSlash = base.rfind('/');
        if (lastSlash == std::string::npos || lastSlash < hostStart)
            return origin + "/" + rel;
        return base.substr(0, lastSlash + 1) + rel;
    }

    std::vector<EpisodeCandidate> AnimeUScraper::parseSeriesPage(const std::string& html) {
        std::vector<EpisodeCandidate> results;

        std::regex epRegex(R"raw(class="episode-item"[^>]*href="([^"]+)"[^>]*>.*?(\d+))raw");

        auto words_begin = std::sregex_iterator(html.begin(), html.end(), epRegex);
        auto words_end = std::sregex_iterator();

        struct Ep {
            int n;
            std::string u;
        };
        std::vector<Ep> eps;

        for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
            std::smatch match = *i;
            eps.push_back({std::stoi(match[2].str()), match[1].str()});
        }

        std::sort(eps.begin(), eps.end(), [](const Ep& a, const Ep& b) { return a.n < b.n; });

        for (const auto& ep : eps)
            results.push_back({ep.n, ep.u});

        return results;
    }

    std::string AnimeUScraper::parseEpisodePage(const std::string& html) {
        std::regex iframeRegex(R"raw(<iframe[^>]*id="embed"[^>]*src="([^"]+)")raw");
        std::smatch iframeMatch;
        if (!std::regex_search(html, iframeMatch, iframeRegex))
            return "";
        return iframeMatch[1].str();
    }

    std::string AnimeUScraper::parseEmbedPage(const std::string& html) {
        std::regex dlRegex(R"raw(window\.downloadUrl\s*=\s*"([^"]+)")raw");
        std::smatch dlMatch;
        if (!std::regex_search(html, dlMatch, dlRegex))
            return "";
        return dlMatch[1].str();
    }

    std::vector<DownloadTask> AnimeUScraper::planSeriesTask(const Series& series,
                                                            std::atomic<bool>&,
                                                            ScraperProgressCb progressCb) {
        std::vector<DownloadTask> results;

        auto episodesMap = ScraperUtils::scanEpisodesMap(series.path);
        int nextNeeded =
            ScraperUtils::computeNextNeeded(series.path, series.lastDownloadedEpisode, episodesMap);

        try {
            cpr::Session session;
            session.SetVerifySsl(cpr::VerifySsl{false});
            session.SetHeader({{"User-Agent", ScraperUtils::platformUserAgent()}});

            cpr::Response seriesPageResp = ScraperUtils::httpGetWithRetry(
                cpr::Url{series.seriesPageUrl}, {{"User-Agent", ScraperUtils::platformUserAgent()}},
                3, 2000);
            if (seriesPageResp.status_code != 200) {
                Logger::warn(std::format("{}: fetch pagina serie fallito (status {})", series.name,
                                         seriesPageResp.status_code));
                return results;
            }
            if (seriesPageResp.text.empty()) {
                Logger::warn(series.name + ": HTML pagina serie vuoto");
                return results;
            }

            std::vector<EpisodeCandidate> eps = parseSeriesPage(seriesPageResp.text);

            if (eps.empty()) {
                Logger::warn(std::format("{}: HTML scaricato ({} byte) ma 0 episodi: markup "
                                         "cambiato?",
                                         series.name, seriesPageResp.text.size()));
                return results;
            }

            size_t totalToCheck = 0;
            for (const auto& ep : eps) {
                int local = series.continueSeries ? (ep.episodeNumber + series.passedEpisodes)
                                                  : ep.episodeNumber;
                if (local >= nextNeeded)
                    totalToCheck++;
            }
            if (progressCb)
                progressCb(std::format("Analisi: {} episodi da verificare", totalToCheck));

            size_t checked = 0;
            for (const auto& ep : eps) {
                int local = series.continueSeries ? (ep.episodeNumber + series.passedEpisodes)
                                                  : ep.episodeNumber;
                if (local >= nextNeeded) {
                    checked++;
                    if (progressCb)
                        progressCb(std::format("Analisi: verifica episodio {}/{}...", checked,
                                               totalToCheck));
                    cpr::Response epPage = ScraperUtils::httpGetWithRetry(
                        cpr::Url{resolveUrl(series.seriesPageUrl, ep.episodeUrl)},
                        {{"User-Agent", ScraperUtils::platformUserAgent()}}, 3, 2000);
                    if (epPage.status_code != 200) {
                        Logger::warn(std::format("{}: Ep {} - fetch pagina episodio fallito "
                                                 "(status {})",
                                                 series.name, ep.episodeNumber,
                                                 epPage.status_code));
                        continue;
                    }

                    std::string iframeUrl = parseEpisodePage(epPage.text);
                    if (iframeUrl.empty()) {
                        Logger::warn(std::format("{}: Ep {} - iframe non trovato", series.name,
                                                 ep.episodeNumber));
                        continue;
                    }

                    cpr::Response iframePage = ScraperUtils::httpGetWithRetry(
                        cpr::Url{iframeUrl}, {{"User-Agent", ScraperUtils::platformUserAgent()}}, 3,
                        2000);
                    if (iframePage.status_code != 200) {
                        Logger::warn(std::format("{}: Ep {} - fetch iframe fallito (status {})",
                                                 series.name, ep.episodeNumber,
                                                 iframePage.status_code));
                        continue;
                    }

                    std::string dlUrl = parseEmbedPage(iframePage.text);
                    if (dlUrl.empty()) {
                        Logger::warn(std::format("{}: Ep {} - download URL non trovato",
                                                 series.name, ep.episodeNumber));
                        continue;
                    }

                    DownloadTask task;
                    task.videoUrl = dlUrl;
                    task.episodeNumber = local;
                    task.shouldProcess = true;
                    task.fileName = std::format("{}_Ep_{}.mp4", series.name, local);
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

} // namespace Core
