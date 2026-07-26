#include "scrapers/AnimeUScraper.hpp"
#include "scrapers/ScraperUtils.hpp"
#include "core/Logger.hpp"
#include <cpr/cpr.h>
#include <regex>
#include <algorithm>
#include <thread>
#include <chrono>

namespace Core {

std::vector<DownloadTask> AnimeUScraper::planSeriesTask(const Series& series) {
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
        if (seriesPageResp.status_code != 200) return results;

        std::regex epRegex(R"raw(class="episode-item"[^>]*href="([^"]+)"[^>]*>.*?(\d+))raw");

        auto words_begin = std::sregex_iterator(seriesPageResp.text.begin(), seriesPageResp.text.end(), epRegex);
        auto words_end = std::sregex_iterator();

        struct Ep { int n; std::string u; };
        std::vector<Ep> eps;

        for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
            std::smatch match = *i;
            eps.push_back({std::stoi(match[2].str()), match[1].str()});
        }

        if (eps.empty()) return results;
        std::sort(eps.begin(), eps.end(), [](const Ep& a, const Ep& b) { return a.n < b.n; });

        for (const auto& ep : eps) {
            int local = series.continueSeries ? (ep.n + series.passedEpisodes) : ep.n;
            if (local >= nextNeeded) {
                cpr::Response epPage = ScraperUtils::httpGetWithRetry(
                    cpr::Url{ep.u},
                    {{"User-Agent", ScraperUtils::platformUserAgent()}},
                    3, 2000
                );
                if (epPage.status_code != 200) continue;

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
                if (iframePage.status_code != 200) continue;

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
