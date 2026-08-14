#include "scrapers/AnimeWScraper.hpp"
#include "core/Logger.hpp"
#include "scrapers/ScraperUtils.hpp"
#include <cpr/cpr.h>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <regex>

using json = nlohmann::json;

namespace Core {

    // Dominio canonico di AnimeWorld (verificato: .so risponde 301, .ac è il dominio attivo).
    // Unico punto di definizione: url e Referer devono restare allineati.
    static const std::string kAnimeWorldBaseUrl = "https://www.animeworld.ac";

    // Base del player, sovrascrivibile via ambiente per i test offline
    // (ANIDOWNLOADER_API_BASE punta alla fixture locale: pagina serie + API).
    static std::string playerApiBase() {
        const char* base = std::getenv("ANIDOWNLOADER_API_BASE");
        return (base && *base) ? std::string(base) : kAnimeWorldBaseUrl;
    }

    // Endpoint statico del player: restituisce l'URL del video (grabber) dato
    // l'id dell'episodio (ultimo segmento dell'URL della pagina episodio).
    // Niente browser: 1 richiesta HTTP con header statici (UA + Referer).
    // Risposta: {"grabber": "...", "name": ..., "target": ...} oppure {"error": true}.
    static std::string episodeInfoUrl() { return playerApiBase() + "/api/episode/info"; }

    // Estrae l'id episodio dall'URL della pagina episodio (es. /play/anime.xyz/ABC123 -> ABC123).
    static std::string extractEpisodeId(const std::string& pageUrl) {
        auto pos = pageUrl.find_last_of('/');
        if (pos == std::string::npos)
            return pageUrl;
        return pageUrl.substr(pos + 1);
    }

    std::vector<EpisodeCandidate> AnimeWScraper::getCandidates(const Series& series) {
        std::vector<EpisodeCandidate> results;

        auto episodesMap = ScraperUtils::scanEpisodesMap(series.path);
        int nextNeeded =
            ScraperUtils::computeNextNeeded(series.path, series.lastDownloadedEpisode, episodesMap);

        cpr::Header staticHeaders = {{"User-Agent", ScraperUtils::platformUserAgent()},
                                     {"Referer", kAnimeWorldBaseUrl + "/"}};

        std::string html = "";
        try {
            cpr::Response r = ScraperUtils::httpGetWithRetry(cpr::Url{series.seriesPageUrl},
                                                             staticHeaders, 3, 2000);
            if (r.status_code == 200)
                html = r.text;
        } catch (const std::exception& e) {
            Core::Logger::warn(series.name +
                               ": errore nel fetch della pagina: " + std::string(e.what()));
        }

        Core::Logger::info(series.name + ": nextNeeded=" + std::to_string(nextNeeded) +
                           ", html_len=" + std::to_string(html.size()));
        if (html.empty()) {
            Core::Logger::warn(series.name + ": HTML statico non scaricabile per getCandidates");
            return results;
        }

        std::regex epTagRegex(R"raw(<a[^>]+data-episode-num=["'](\d+)["'][^>]*>)raw");
        struct EpData {
            int n;
            std::string url;
        };
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

        Core::Logger::info(series.name + ": regex found " + std::to_string(found.size()) +
                           " episode tags");
        if (found.empty()) {
            // Sample diagnostico: il primo tag <a> con data-episode-num o, in
            // assenza, il primo <a> con href; fallback primi 300 char dell'HTML.
            std::string sample;
            std::smatch sm;
            static const std::regex sampleEpRegex(R"raw(<a[^>]*data-episode-num[^>]*>)raw",
                                                  std::regex_constants::icase);
            static const std::regex sampleHrefRegex(R"raw(<a[^>]*href[^>]*>)raw",
                                                    std::regex_constants::icase);
            if (std::regex_search(html, sm, sampleEpRegex))
                sample = sm.str(0);
            else if (std::regex_search(html, sm, sampleHrefRegex))
                sample = sm.str(0);
            else
                sample = html.substr(0, 300);
            if (sample.size() > 300)
                sample = sample.substr(0, 300);
            std::string clean;
            for (char c : sample)
                if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F)
                    clean += c;

            Core::Logger::warn(series.name + ": nessun episodio nell'HTML statico (" +
                               std::to_string(html.size()) + " byte) — sample: " + clean);
            return results;
        }
        std::sort(found.begin(), found.end(),
                  [](const EpData& a, const EpData& b) { return a.n < b.n; });

        for (const auto& ep : found) {
            int local = series.continueSeries ? (ep.n + series.passedEpisodes) : ep.n;
            Core::Logger::info(series.name + ": candidate[local=" + std::to_string(local) +
                               "] pageUrl=" + ep.url +
                               " (da data-episode-num=" + std::to_string(ep.n) + " e href)");
            if (local >= nextNeeded) {
                results.push_back({local, ep.url});
            } else {
                Core::Logger::info(series.name + ": filtered out Ep." + std::to_string(local) +
                                   " < nextNeeded=" + std::to_string(nextNeeded));
            }
        }

        Core::Logger::info(series.name + ": final candidates=" + std::to_string(results.size()));
        return results;
    }

    std::vector<DownloadTask> AnimeWScraper::planSeriesTask(const Series& series,
                                                            std::atomic<bool>& stopSignal,
                                                            ScraperProgressCb progressCb) {
        std::vector<DownloadTask> results;

        auto candidates = getCandidates(series);
        Core::Logger::info(series.name + ": getCandidates returned " +
                           std::to_string(candidates.size()) + " candidates");
        if (candidates.empty()) {
            Core::Logger::warn(series.name + ": getCandidates empty, skipping");
            return results;
        }

        if (progressCb)
            progressCb("Analisi: " + std::to_string(candidates.size()) +
                       " episodi da verificare...");

        cpr::Header apiHeaders = {{"User-Agent", ScraperUtils::platformUserAgent()},
                                  {"Referer", playerApiBase() + "/"}};

        // Flusso statico: per ogni episodio mancante una sola richiesta HTTP
        // all'endpoint del player (nessun browser).
        size_t checked = 0;
        for (const auto& candidate : candidates) {
            if (stopSignal.load())
                break;
            checked++;
            if (progressCb)
                progressCb("Analisi: verifica episodio " + std::to_string(checked) + "/" +
                           std::to_string(candidates.size()) + "...");

            std::string episodeId = extractEpisodeId(candidate.episodeUrl);
            std::string apiUrl = episodeInfoUrl() + "?id=" + episodeId + "&alt=1";

            std::string grabber;
            try {
                cpr::Response r =
                    ScraperUtils::httpGetWithRetry(cpr::Url{apiUrl}, apiHeaders, 3, 2000);
                if (r.status_code == 200 && !r.text.empty()) {
                    auto j = json::parse(r.text);
                    if (!j.contains("error") && j.contains("grabber")) {
                        grabber = j["grabber"].get<std::string>();
                    }
                } else {
                    Core::Logger::warn(
                        series.name + ": Ep." + std::to_string(candidate.episodeNumber) +
                        " - API episodio fallita (status " + std::to_string(r.status_code) + ")");
                }
            } catch (const std::exception& e) {
                Core::Logger::warn(series.name + ": Ep." + std::to_string(candidate.episodeNumber) +
                                   " - errore API episodio: " + std::string(e.what()));
            }

            if (grabber.empty()) {
                Core::Logger::warn(series.name + ": Ep." + std::to_string(candidate.episodeNumber) +
                                   " - nessun URL video restituito dall'API");
                continue;
            }

            Core::Logger::info(series.name + ": verifica episodio completata -> [episodeNumber=" +
                               std::to_string(candidate.episodeNumber) + "] videoUrl=" + grabber);

            DownloadTask task;
            task.shouldProcess = true;
            task.videoUrl = grabber;
            task.episodeNumber = candidate.episodeNumber;
            task.fileName = ScraperUtils::generateFilename(grabber, candidate.episodeNumber);
            results.push_back(task);
        }

        Core::Logger::info(series.name + ": planSeriesTask complete, returning " +
                           std::to_string(results.size()) + " tasks");
        return results;
    }

} // namespace Core
