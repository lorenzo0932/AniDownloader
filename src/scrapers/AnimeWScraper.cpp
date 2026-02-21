#include "scrapers/AnimeWScraper.hpp"
#include "scrapers/ScraperUtils.hpp"
#include <cpr/cpr.h>
#include <regex>
#include <algorithm>

namespace Core {

DownloadTask AnimeWScraper::planSeriesTask(const Series& series) {
    DownloadTask task;
    task.shouldProcess = false;

    cpr::Header headers = {
        {"User-Agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36"},
        {"Referer", "https://www.animeworld.so/"}
    };

    try {
        cpr::Response r = cpr::Get(cpr::Url{series.seriesPageUrl}, headers, cpr::VerifySsl{false}, cpr::Timeout{15000});
        if (r.status_code != 200) return task;

        // Estrazione Episodi
        std::regex epTagRegex(R"raw(<a[^>]+data-episode-num=["'](\d+)["'][^>]*>)raw");
        struct EpData { int n; std::string u; };
        std::vector<EpData> found;

        auto it = std::sregex_iterator(r.text.begin(), r.text.end(), epTagRegex);
        for (; it != std::sregex_iterator(); ++it) {
            std::regex hrefRegex(R"raw(href=["']([^"']+)["'])raw");
            std::smatch m;
            std::string tag = (*it)[0].str();
            if (std::regex_search(tag, m, hrefRegex)) found.push_back({std::stoi((*it)[1].str()), m[1].str()});
        }

        if (found.empty()) return task;
        std::sort(found.begin(), found.end(), [](const EpData& a, const EpData& b) { return a.n < b.n; });

        int nextNeeded = ScraperUtils::getNextEpisodeNum(series.path);

        EpData target = {0, ""};
        int finalEpNum = 0;
        for (const auto& ep : found) {
            int local = series.continueSeries ? (ep.n + series.passedEpisodes) : ep.n;
            if (local >= nextNeeded) { target = ep; finalEpNum = local; break; }
        }

        if (target.u.empty()) return task;

        // Link Download
        std::string epPageUrl = series.seriesPageUrl.substr(0, series.seriesPageUrl.find("/", 8)) + target.u;
        cpr::Response r_ep = cpr::Get(cpr::Url{epPageUrl}, headers, cpr::VerifySsl{false});

        std::string dlUrl = "";
        std::regex dlRegex(R"raw(alternativeDownloadLink["'][^>]+href=["']([^"']+)["'])raw");
        std::regex dlRegexAlt(R"raw(href=["']([^"']+)["'][^>]+alternativeDownloadLink)raw");
        std::smatch m_dl;

        if (std::regex_search(r_ep.text, m_dl, dlRegex) || std::regex_search(r_ep.text, m_dl, dlRegexAlt)) {
            dlUrl = m_dl[1].str();
        } else {
            std::regex txtRegex(R"raw(<a[^>]+href=["']([^"']+)["'][^>]*>[^<]*Download Alternativo[^<]*</a>)raw", std::regex_constants::icase);
            if (std::regex_search(r_ep.text, m_dl, txtRegex)) dlUrl = m_dl[1].str();
        }

        if (!dlUrl.empty()) {
            task.shouldProcess = true;
            task.videoUrl = (dlUrl.find("http") == 0) ? dlUrl : series.seriesPageUrl.substr(0, series.seriesPageUrl.find("/", 8)) + dlUrl;
            task.episodeNumber = finalEpNum;
            task.fileName = ScraperUtils::generateFilename(task.videoUrl, series.name, finalEpNum);
        }
    } catch (...) {}
    return task;
}
}