#include "scrapers/AnimeWScraper.hpp"
#include "scrapers/ScraperUtils.hpp"
#include "config/AppConfigManager.hpp"
#include "core/Logger.hpp"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <regex>
#include <atomic>

using json = nlohmann::json;

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

static json cdpCommand(const std::string& sessionId, const std::string& cmd, const json& params = {}) {
    return webdriverCommand("/session/" + sessionId + "/chromium/send_command_and_get_result", "POST", {
        {"cmd", cmd},
        {"params", params}
    });
}

static std::string openNewTab(const std::string& sessionId) {
    json res = webdriverCommand("/session/" + sessionId + "/window/new", "POST", json::object());
    if (res.contains("value") && res["value"].contains("handle")) {
        return res["value"]["handle"].get<std::string>();
    }
    return {};
}

static void switchToWindow(const std::string& sessionId, const std::string& handle) {
    webdriverCommand("/session/" + sessionId + "/window", "POST", {{"handle", handle}});
}

static void closeTab(const std::string& sessionId, const std::string& handle) {
    switchToWindow(sessionId, handle);
    webdriverCommand("/session/" + sessionId + "/window", "DELETE");
}

static json getFrameTree(const std::string& sessionId) {
    return cdpCommand(sessionId, "Page.getFrameTree", json::object());
}

static std::string extractUrlFromLog(const std::string& msgStr);

enum class SniffState {
    NAVIGATING, WAITING_ALT_CLICK,
    WAITING_IFRAME, SNIFFING, DONE, ERROR
};

struct SniffTab {
    std::string windowHandle;
    std::string episodeUrl;
    int episodeNumber;
    std::string videoUrl;
    SniffState state;
    std::chrono::steady_clock::time_point stateEntered;
    std::vector<std::string> frameIds;
};

static void collectFrameIdsRecursive(const json& node, std::vector<std::string>& out) {
    if (node.contains("frame") && node["frame"].contains("id")) {
        out.push_back(node["frame"]["id"].get<std::string>());
    }
    if (node.contains("childFrames") && node["childFrames"].is_array()) {
        for (const auto& child : node["childFrames"]) {
            collectFrameIdsRecursive(child, out);
        }
    }
}

static std::vector<DownloadTask> sniffBatch(const std::string& sessionId,
    const std::vector<EpisodeCandidate>& candidates,
    std::atomic<bool>& stopSignal, const std::string& seriesName)
{
    std::vector<DownloadTask> results;
    if (candidates.empty() || stopSignal.load()) return results;

    std::vector<SniffTab> tabs;
    for (const auto& c : candidates) {
        if (stopSignal.load()) break;
        std::string wh = openNewTab(sessionId);
        if (wh.empty()) {
            Core::Logger::warn(seriesName + ": impossibile aprire tab per Ep." + std::to_string(c.episodeNumber));
            continue;
        }
        switchToWindow(sessionId, wh);
        std::string fullUrl = (c.episodeUrl.find("http") == 0) ? c.episodeUrl : "https://www.animeworld.ac" + c.episodeUrl;
        webdriverCommand("/session/" + sessionId + "/url", "POST", {{"url", fullUrl}});
        tabs.push_back({wh, fullUrl, c.episodeNumber, {}, SniffState::NAVIGATING,
                        std::chrono::steady_clock::now(), {}});
    }

    if (!tabs.empty()) {
        try {
            auto poolStart = std::chrono::steady_clock::now();

            // MAPPA GLOBALE: frameId -> Lista di URL video trovati
            std::map<std::string, std::vector<std::string>> capturedUrlsByFrame;

            while (!stopSignal.load()) {
                bool allDone = true;

                // --- 1. LETTURA LOG GLOBALE (Assegna gli URL alle rispettive "cassette della posta") ---
                json logRes = webdriverCommand("/session/" + sessionId + "/log", "POST", {{"type", "performance"}});
                if (logRes.contains("value") && logRes["value"].is_array()) {
                    for (auto& entry : logRes["value"]) {
                        if (!entry.contains("message")) continue;
                        std::string msgStr = entry["message"].get<std::string>();
                        std::string url = extractUrlFromLog(msgStr);

                        if (!url.empty()) {
                            std::string frameId;
                            try {
                                json inner = json::parse(msgStr);
                                if (inner.contains("message") && inner["message"].contains("params") &&
                                    inner["message"]["params"].contains("frameId")) {
                                    frameId = inner["message"]["params"]["frameId"].get<std::string>();
                                }
                            } catch (...) {} // ignora errori di parsing json

                            if (!frameId.empty()) {
                                capturedUrlsByFrame[frameId].push_back(url);
                            } else {
                                Core::Logger::warn(seriesName + ": Trovato URL senza frameId -> " + url);
                            }
                        }
                    }
                }

                // --- 2. MACCHINA A STATI DEI TAB ---
                for (auto& tab : tabs) {
                    if (tab.state == SniffState::DONE || tab.state == SniffState::ERROR) continue;
                    allDone = false;

                    switchToWindow(sessionId, tab.windowHandle);
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - tab.stateEntered).count();

                    // Aggiorna costantemente la lista degli iframe figli di questo tab
                    json ft = getFrameTree(sessionId);
                    if (ft.contains("value") && ft["value"].contains("frameTree")) {
                        tab.frameIds.clear();
                        collectFrameIdsRecursive(ft["value"]["frameTree"], tab.frameIds);
                    }

                    // Controlla se il log globale ha catturato un URL per uno degli iframe di QUESTO tab
                    for (const auto& fid : tab.frameIds) {
                        if (capturedUrlsByFrame.count(fid) && !capturedUrlsByFrame[fid].empty()) {
                            tab.videoUrl = capturedUrlsByFrame[fid].front();
                            tab.state = SniffState::DONE;
                            break;
                        }
                    }
                    if (tab.state == SniffState::DONE) continue; // URL trovato, vai al prossimo tab!

                    // Logica standard di navigazione pagina
                    switch (tab.state) {
                        case SniffState::NAVIGATING: {
                            json eval = cdpCommand(sessionId, "Runtime.evaluate", {
                                {"expression", "document.readyState === 'complete' || document.readyState === 'interactive'"},
                                {"returnByValue", true}
                            });
                            bool ready = false;
                            if (eval.contains("value") && eval["value"].contains("result") &&
                                eval["value"]["result"].contains("value")) {
                                ready = eval["value"]["result"]["value"].get<bool>();
                            }
                            if (ready || elapsed > 15) {
                                cdpCommand(sessionId, "Network.enable", json::object());
                                json altSearch = webdriverCommand("/session/" + sessionId + "/elements", "POST", {
                                    {"using", "css selector"}, {"value", "#alternative"}
                                });
                                if (altSearch.contains("value") && altSearch["value"].is_array() && !altSearch["value"].empty()) {
                                    std::string altId = altSearch["value"][0].begin().value().get<std::string>();
                                    webdriverCommand("/session/" + sessionId + "/element/" + altId + "/click", "POST", json::object());
                                    tab.state = SniffState::WAITING_ALT_CLICK;
                                } else {
                                    tab.state = SniffState::WAITING_IFRAME;
                                }
                                tab.stateEntered = std::chrono::steady_clock::now();
                            }
                            break;
                        }
                        case SniffState::WAITING_ALT_CLICK:
                            if (elapsed >= 2) {
                                tab.state = SniffState::WAITING_IFRAME;
                                tab.stateEntered = std::chrono::steady_clock::now();
                            }
                            break;
                        case SniffState::WAITING_IFRAME: {
                            json iframeSearch = webdriverCommand("/session/" + sessionId + "/elements", "POST", {
                                {"using", "css selector"}, {"value", "iframe#player-iframe"}
                            });
                            bool found = iframeSearch.contains("value") && iframeSearch["value"].is_array() && !iframeSearch["value"].empty();
                            if (found) {
                                std::string fId = iframeSearch["value"][0].begin().value().get<std::string>();
                                webdriverCommand("/session/" + sessionId + "/element/" + fId + "/click", "POST", json::object());
                                tab.state = SniffState::SNIFFING;
                                tab.stateEntered = std::chrono::steady_clock::now();
                            } else if (elapsed > 10) {
                                Core::Logger::warn(seriesName + ": Ep." + std::to_string(tab.episodeNumber) + " iframe timeout");
                                tab.state = SniffState::ERROR;
                            }
                            break;
                        }
                        case SniffState::SNIFFING: {
                            if (elapsed > 10) {
                                tab.state = SniffState::DONE; // Timeout sniffer
                            }
                            break;
                        }
                        default: break;
                    }
                }

                if (allDone) break;
                if (std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - poolStart).count() > 90) {
                    Core::Logger::warn(seriesName + ": sniff batch timeout globale");
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            for (auto& tab : tabs) {
                if (!tab.videoUrl.empty()) {
                    Core::Logger::info(seriesName + ": Sniff completato -> [episodeNumber=" + std::to_string(tab.episodeNumber) +
                                       "] pageUrl=" + tab.episodeUrl + " -> videoUrl=" + tab.videoUrl);
                    DownloadTask task;
                    task.shouldProcess = true;
                    task.videoUrl = tab.videoUrl;
                    task.episodeNumber = tab.episodeNumber;
                    task.fileName = ScraperUtils::generateFilename(tab.videoUrl, tab.episodeNumber);
                    results.push_back(task);
                }
            }
        } catch (const std::exception& e) {
            Core::Logger::warn(seriesName + ": sniffBatch eccezione: " + std::string(e.what()));
        }
    }

    for (auto& tab : tabs) {
        closeTab(sessionId, tab.windowHandle);
    }
    return results;
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

std::vector<EpisodeCandidate> AnimeWScraper::getCandidates(const Series& series) {
    std::vector<EpisodeCandidate> results;

    auto episodesMap = ScraperUtils::scanEpisodesMap(series.path);
    int nextNeeded = ScraperUtils::computeNextNeeded(series.path, series.lastDownloadedEpisode, episodesMap);

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

    Core::Logger::info(series.name + ": nextNeeded=" + std::to_string(nextNeeded) + ", html_len=" + std::to_string(html.size()));
    if (html.empty()) {
        Core::Logger::warn(series.name + ": HTML statico non scaricabile per getCandidates");
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

    Core::Logger::info(series.name + ": regex found " + std::to_string(found.size()) + " episode tags");
    if (found.empty()) {
        Core::Logger::warn(series.name + ": nessun episodio nell'HTML statico");
        return results;
    }
    std::sort(found.begin(), found.end(), [](const EpData& a, const EpData& b) { return a.n < b.n; });

    for (const auto& ep : found) {
        int local = series.continueSeries ? (ep.n + series.passedEpisodes) : ep.n;
        Core::Logger::info(series.name + ": candidate[local=" + std::to_string(local) +
                           "] pageUrl=" + ep.url + " (da data-episode-num=" + std::to_string(ep.n) + " e href)");
        if (local >= nextNeeded) {
            results.push_back({local, ep.url});
        } else {
            Core::Logger::info(series.name + ": filtered out Ep." + std::to_string(local) + " < nextNeeded=" + std::to_string(nextNeeded));
        }
    }

    Core::Logger::info(series.name + ": final candidates=" + std::to_string(results.size()));
    return results;
}

std::vector<DownloadTask> AnimeWScraper::planSeriesTask(const Series& series, std::atomic<bool>& stopSignal,
    ScraperProgressCb progressCb)
{
    std::vector<DownloadTask> results;

    auto candidates = getCandidates(series);
    Core::Logger::info(series.name + ": getCandidates returned " + std::to_string(candidates.size()) + " candidates");
    if (candidates.empty()) {
        Core::Logger::warn(series.name + ": getCandidates empty, skipping");
        return results;
    }

    if (progressCb) progressCb("Analisi: " + std::to_string(candidates.size()) + " episodi da verificare, attesa risorsa Chrome...");
    Core::Logger::info(series.name + ": waiting for ScraperSemaphoreGuard...");
    ScraperSemaphoreGuard scraperGuard;
    Core::Logger::info(series.name + ": ScraperSemaphoreGuard acquired");

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

    if (progressCb) progressCb("Analisi: creazione sessione ChromeDriver...");
    Core::Logger::info(series.name + ": creating ChromeDriver session...");
    json sessionRes;
    std::string sessionId;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        sessionRes = webdriverCommand("/session", "POST", caps);
        if (sessionRes.contains("value") && sessionRes["value"].contains("sessionId")) {
            sessionId = sessionRes["value"]["sessionId"].get<std::string>();
            Core::Logger::info(series.name + ": ChromeDriver session created: " + sessionId);
            break;
        }
        if (attempt < 3) {
            Core::Logger::warn("ChromeDriver session fallita, tentativo " + std::to_string(attempt) + "/3");
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    if (sessionId.empty()) {
        if (progressCb) progressCb("Analisi: errore creazione sessione ChromeDriver");
        Core::Logger::warn(series.name + ": ChromeDriver session creation FAILED after 3 attempts");
        DownloadTask err;
        err.errorMessage = "Impossibile connettersi a ChromeDriver locale (Porta 9515).";
        results.push_back(err);
        return results;
    }

    unsigned int hwThreads = std::thread::hardware_concurrency();
    int maxTabs = (hwThreads == 0) ? 2 : (hwThreads < 6) ? 1 : (hwThreads <= 12) ? 3 : (hwThreads <= 24) ? 4 : 6;
    Core::Logger::info(series.name + ": maxTabs=" + std::to_string(maxTabs));

    size_t totalBatches = (candidates.size() + maxTabs - 1) / maxTabs;
    for (size_t offset = 0; offset < candidates.size(); offset += maxTabs) {
        if (stopSignal.load()) break;

        size_t batchNum = offset / maxTabs + 1;
        size_t remaining = candidates.size() - offset;
        size_t batchSize = std::min(static_cast<size_t>(maxTabs), remaining);
        std::vector<EpisodeCandidate> batch(
            candidates.begin() + static_cast<std::ptrdiff_t>(offset),
            candidates.begin() + static_cast<std::ptrdiff_t>(offset + batchSize)
        );

        if (progressCb) progressCb("Analisi: sniffing batch " + std::to_string(batchNum) + "/" + std::to_string(totalBatches) + " (" + std::to_string(batchSize) + " ep)...");

        try {
            auto batchResults = sniffBatch(sessionId, batch, stopSignal, series.name);
            results.insert(results.end(), batchResults.begin(), batchResults.end());
            if (progressCb) progressCb("Analisi: batch " + std::to_string(batchNum) + "/" + std::to_string(totalBatches) + " completato (" + std::to_string(batchResults.size()) + " URL)");
        } catch (const std::exception& e) {
            Core::Logger::warn(series.name + ": batch sniff fallito: " + std::string(e.what()));
            if (progressCb) progressCb("Analisi: batch " + std::to_string(batchNum) + "/" + std::to_string(totalBatches) + " fallito");
            break;
        }
    }

    Core::Logger::info(series.name + ": planSeriesTask complete, returning " + std::to_string(results.size()) + " tasks");
    try {
        webdriverCommand("/session/" + sessionId, "DELETE");
        Core::Logger::info(series.name + ": ChromeDriver session deleted");
    } catch (...) {
        Core::Logger::warn(series.name + ": ChromeDriver session delete failed");
    }
    return results;
}

} // namespace Core