#include "scrapers/AnimeWScraper.hpp"
#include "scrapers/ScraperUtils.hpp"
#include "core/Logger.hpp"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <regex>

using json = nlohmann::json;
static const int MAX_BATCH_SIZE = 5;

namespace Core {

static json webdriverCommand(const std::string& endpoint, const std::string& method, const json& payload = {}) {
    std::string url = "http://localhost:9515" + endpoint;
    cpr::Response r;
    cpr::Header headers = {{"Content-Type", "application/json"}};

    if (method == "POST") {
        r = cpr::Post(cpr::Url{url}, cpr::Body{payload.dump()}, headers);
    } else if (method == "GET") {
        r = cpr::Get(cpr::Url{url});
    } else if (method == "DELETE") {
        r = cpr::Delete(cpr::Url{url});
    }

    if (r.status_code == 200 && !r.text.empty()) {
        try { return json::parse(r.text); } catch (...) { return json(); }
    }
    return json();
}

static std::string extractUrlFromLog(const std::string& msgStr) {
    std::regex urlRegex(R"raw(https?:\\?/\\?/[^\s"']+)raw");
    std::smatch match;
    std::string::const_iterator searchStart(msgStr.cbegin());

    while (std::regex_search(searchStart, msgStr.cend(), match, urlRegex)) {
        std::string foundUrl = match[0].str();
        std::string cleanUrl = "";
        for (char c : foundUrl) {
            if (c != '\\') cleanUrl += c;
        }
        if (!cleanUrl.empty() && (cleanUrl.back() == '"' || cleanUrl.back() == '\'')) {
            cleanUrl.pop_back();
        }
        if ((cleanUrl.find(".mp4") != std::string::npos || cleanUrl.find("m3u8") != std::string::npos) &&
            cleanUrl.find("blob:") == std::string::npos) {
            return cleanUrl;
        }
        searchStart = match.suffix().first;
    }
    return "";
}

static std::string sniffVideoUrl(const std::string& sessionId, const std::string& episodeUrl) {
    std::string fullUrl = (episodeUrl.find("http") == 0) ? episodeUrl : "https://www.animeworld.ac" + episodeUrl;
    webdriverCommand("/session/" + sessionId + "/url", "POST", {{"url", fullUrl}});

    std::string altBtnId = "";
    for (int i = 0; i < 5; ++i) {
        json altSearch = webdriverCommand("/session/" + sessionId + "/elements", "POST", {
            {"using", "css selector"}, {"value", "#alternative"}
        });
        if (altSearch.contains("value") && altSearch["value"].is_array() && !altSearch["value"].empty()) {
            altBtnId = altSearch["value"][0].begin().value().get<std::string>();
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    if (!altBtnId.empty()) {
        webdriverCommand("/session/" + sessionId + "/element/" + altBtnId + "/click", "POST", json::object());
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    std::string frameId = "";
    for (int i = 0; i < 10; ++i) {
        json iframeSearch = webdriverCommand("/session/" + sessionId + "/elements", "POST", {
            {"using", "css selector"}, {"value", "iframe#player-iframe"}
        });
        if (iframeSearch.contains("value") && iframeSearch["value"].is_array() && !iframeSearch["value"].empty()) {
            frameId = iframeSearch["value"][0].begin().value().get<std::string>();
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    if (!frameId.empty()) {
        webdriverCommand("/session/" + sessionId + "/element/" + frameId + "/click", "POST", json::object());
    }

    std::string dlUrl = "";
    for (int i = 0; i < 15; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        json logRes = webdriverCommand("/session/" + sessionId + "/log", "POST", {{"type", "performance"}});
        if (logRes.contains("value") && logRes["value"].is_array()) {
            for (auto& entry : logRes["value"]) {
                if (!entry.contains("message")) continue;
                std::string msgStr = entry["message"].get<std::string>();
                std::string candidateUrl = extractUrlFromLog(msgStr);
                if (!candidateUrl.empty()) {
                    dlUrl = candidateUrl;
                    break;
                }
            }
        }
        if (!dlUrl.empty()) break;
    }

    return dlUrl;
}

std::vector<DownloadTask> AnimeWScraper::planSeriesTask(const Series& series) {
    std::vector<DownloadTask> results;

    // FASE 1: Scansione directory + calcolo prossimo episodio
    auto episodesMap = ScraperUtils::scanEpisodesMap(series.path);
    int nextNeeded = ScraperUtils::computeNextNeeded(series.path, series.lastDownloadedEpisode, episodesMap);

    // FASE 2: Download HTML statico ed estrazione episodi online
    cpr::Header staticHeaders = {
        {"User-Agent", ScraperUtils::platformUserAgent()},
        {"Referer", "https://www.animeworld.so/"}
    };

    std::string html = "";
    try {
        cpr::Response r = ScraperUtils::httpGetWithRetry(
            cpr::Url{series.seriesPageUrl}, staticHeaders, 3, 2000
        );
        if (r.status_code == 200) html = r.text;
    } catch (...) {}

    if (html.empty()) {
        DownloadTask err;
        err.errorMessage = "Errore: Impossibile scaricare l'HTML statico per la verifica preliminare.";
        results.push_back(err);
        return results;
    }

    std::regex epTagRegex(R"raw(<a[^>]+data-episode-num=["'](\d+)["'][^>]*>)raw");
    struct EpData { int n; std::string url; };
    std::vector<EpData> found;

    auto it = std::sregex_iterator(html.begin(), html.end(), epTagRegex);
    for (; it != std::sregex_iterator(); ++it) {
        std::regex hrefRegex(R"raw(href=["']([^"']+)["'])raw");
        std::smatch m;
        std::string tag = (*it)[0].str();
        if (std::regex_search(tag, m, hrefRegex)) {
            found.push_back({std::stoi((*it)[1].str()), m[1].str()});
        }
    }

    if (found.empty()) {
        DownloadTask err;
        err.errorMessage = "Nessun episodio rilevato nell'HTML statico.";
        results.push_back(err);
        return results;
    }
    std::sort(found.begin(), found.end(), [](const EpData& a, const EpData& b) { return a.n < b.n; });

    // FASE 3: Seleziona candidati (episodi online >= nextNeeded)
    std::vector<std::pair<EpData, int>> candidates;
    for (const auto& ep : found) {
        int local = series.continueSeries ? (ep.n + series.passedEpisodes) : ep.n;
        if (local >= nextNeeded) {
            candidates.push_back({ep, local});
        }
    }

    if (candidates.empty()) {
        return results;
    }

    // FASE 4: ChromeDriver batch — una sessione per tutti gli episodi consecutivi
    ScraperSemaphoreGuard scraperGuard;

    json caps = {
        {"capabilities", {
            {"alwaysMatch", {
                {"pageLoadStrategy", "eager"},
                {"goog:chromeOptions", {
                    {"args", {
                        "--headless=new",
                        "--no-sandbox",
                        "--disable-dev-shm-usage",
                        "--autoplay-policy=no-user-gesture-required",
                        "--disable-blink-features=AutomationControlled",
                        "--mute-audio",
                        "--user-agent=" + ScraperUtils::platformUserAgent()
                    }},
                    {"excludeSwitches", {"enable-automation"}},
                    {"perfLoggingPrefs", {{"enableNetwork", true}}}
                }},
                {"goog:loggingPrefs", {{"performance", "ALL"}}}
            }}
        }}
    };

    json sessionRes;
    std::string sessionId;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        sessionRes = webdriverCommand("/session", "POST", caps);
        if (sessionRes.contains("value") && sessionRes["value"].contains("sessionId")) {
            sessionId = sessionRes["value"]["sessionId"].get<std::string>();
            break;
        }
        if (attempt < 3) {
            Core::Logger::warn("ChromeDriver session fallita, tentativo " + std::to_string(attempt) + "/3");
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    if (sessionId.empty()) {
        DownloadTask err;
        err.errorMessage = "Impossibile connettersi a ChromeDriver locale (Porta 9515).";
        results.push_back(err);
        return results;
    }

    int batchCount = 0;
    for (const auto& [ep, localEpNum] : candidates) {
        if (batchCount >= MAX_BATCH_SIZE) break;

        try {
            std::string dlUrl = sniffVideoUrl(sessionId, ep.url);
            if (dlUrl.empty()) break;

            DownloadTask task;
            task.shouldProcess = true;
            task.videoUrl = dlUrl;
            task.episodeNumber = localEpNum;
            task.fileName = ScraperUtils::generateFilename(dlUrl, localEpNum);
            results.push_back(task);
            batchCount++;

        } catch (const std::exception& e) {
            Core::Logger::warn(series.name + ": Ep." + std::to_string(localEpNum) + " sniff fallito: " + e.what());
            break;
        }
    }

    webdriverCommand("/session/" + sessionId, "DELETE");
    return results;
}

} // namespace Core
