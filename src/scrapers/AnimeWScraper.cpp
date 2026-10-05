#include "scrapers/AnimeWScraper.hpp"
#include "core/Logger.hpp"
#include "scrapers/ScraperUtils.hpp"
#include <cpr/cpr.h>
#include <cstdlib>
#include <cstring>
#include <format>
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

    std::vector<EpisodeCandidate> AnimeWScraper::parseSeriesPage(const std::string& html) {
        std::vector<EpisodeCandidate> results;

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

        Core::Logger::info(std::format("AnimeW: regex found {} episode tags", found.size()));
        if (found.empty()) {
            // Sample diagnostico: il primo tag <a> con data-episode-num o, in
            // assenza, il primo <a> con href; fallback primi 300 char dell'HTML.
            std::string sample;
            std::smatch sm;
            static const std::regex sampleEpRegex(R"raw(<a[^>]*data-episode-num[^>]*>)raw",
                                                  std::regex_constants::icase);
            static const std::regex sampleHrefRegex(R"raw(<a[^>]*href[^>]*>)raw",
                                                    std::regex_constants::icase);
            if (std::regex_search(html, sm, sampleEpRegex) ||
                std::regex_search(html, sm, sampleHrefRegex))
                sample = sm.str(0);
            else
                sample = html.substr(0, 300);
            if (sample.size() > 300)
                sample = sample.substr(0, 300);
            std::string clean;
            for (char c : sample)
                if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F)
                    clean += c;

            Core::Logger::warn(std::format("AnimeW: nessun episodio nell'HTML statico ({} byte) — "
                                           "sample: {}",
                                           html.size(), clean));
            return results;
        }
        std::sort(found.begin(), found.end(),
                  [](const EpData& a, const EpData& b) { return a.n < b.n; });

        for (const auto& ep : found)
            results.push_back({ep.n, ep.url});

        return results;
    }

    std::string AnimeWScraper::parseEpisodeInfo(const std::string& body) {
        if (body.empty())
            return "";
        try {
            auto j = json::parse(body);
            if (!j.contains("error") && j.contains("grabber"))
                return j["grabber"].get<std::string>();
        } catch (const std::exception&) {
            // body non JSON: intenzionale, nessun URL video — gestito dal chiamante
        }
        return "";
    }

    // ---- METADATI SERIE ----
    // Normalizza spazi (il markup ha newline + indentazione) e decodifica le
    // poche entità HTML che il sito usa nei testi.
    static std::string cleanMetaText(std::string v) {
        // strip tag annidati (es. <div class="watchlist-tag">Anime</div> nel Tipo)
        static const std::regex tagRegex(R"(<[^>]*>)");
        v = std::regex_replace(v, tagRegex, "");
        const std::pair<const char*, const char*> table[] = {
            {"&amp;", "&"}, {"&quot;", "\""}, {"&#039;", "'"}, {"&#x27;", "'"},
            {"&apos;", "'"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&nbsp;", " "}};
        for (const auto& [ent, ch] : table) {
            size_t pos = 0;
            while ((pos = v.find(ent, pos)) != std::string::npos)
                v.replace(pos, strlen(ent), ch);
        }
        std::string out;
        bool space = false;
        for (char c : v) {
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
                if (!out.empty() && !space) {
                    out += ' ';
                    space = true;
                }
            } else {
                out += c;
                space = false;
            }
        }
        if (!out.empty() && out.back() == ' ')
            out.pop_back();
        return out;
    }

    // Contenuto grezzo del <dd> che segue <dt>Etichetta:</dt>.
    static std::string dtDdBlock(const std::string& html, const std::string& label) {
        std::regex re("<dt>\\s*" + label + ":\\s*</dt>\\s*<dd>([\\s\\S]*?)</dd>",
                      std::regex_constants::icase);
        std::smatch m;
        if (!std::regex_search(html, m, re))
            return "";
        return m[1].str();
    }

    AnimeWScraper::SeriesMetadata AnimeWScraper::parseSeriesMetadata(const std::string& html) {
        SeriesMetadata meta;
        if (html.empty())
            return meta;

        static const std::regex ogImageRegex(
            R"raw(<meta[^>]+og:image"[^>]*content="([^"]+)")raw",
            std::regex_constants::icase);
        static const std::regex descRegex(R"raw(<div\s+class="desc">([\s\S]*?)</div>)raw",
                                          std::regex_constants::icase);
        static const std::regex genreLinkRegex(R"raw(<a[^>]*>([^<]+)</a>)raw",
                                               std::regex_constants::icase);
        std::smatch m;
        if (std::regex_search(html, m, ogImageRegex))
            meta.posterUrl = m[1].str();
        if (std::regex_search(html, m, descRegex))
            meta.trama = cleanMetaText(m[1].str());
        meta.stato = cleanMetaText(dtDdBlock(html, "Stato"));
        meta.anno = cleanMetaText(dtDdBlock(html, "Anno"));
        std::string generiBlock = dtDdBlock(html, "Generi");
        if (!generiBlock.empty()) {
            for (auto it = std::sregex_iterator(generiBlock.begin(), generiBlock.end(),
                                                genreLinkRegex);
                 it != std::sregex_iterator(); ++it)
                meta.generi.push_back(cleanMetaText((*it)[1].str()));
        }
        return meta;
    }

    AnimeWScraper::SeriesMetadata
    AnimeWScraper::fetchSeriesMetadata(const std::string& pageUrl) {
        SeriesMetadata meta;
        if (pageUrl.empty())
            return meta;
        cpr::Header headers = {{"User-Agent", ScraperUtils::platformUserAgent()},
                               {"Referer", kAnimeWorldBaseUrl + "/"}};
        try {
            cpr::Response r = ScraperUtils::httpGetWithRetry(cpr::Url{pageUrl}, headers, 3, 2000);
            if (r.status_code == 200 && !r.text.empty())
                meta = parseSeriesMetadata(r.text);
            else
                Core::Logger::warn(std::format("Metadati: fetch {} fallito (status {})", pageUrl,
                                               r.status_code));
        } catch (const std::exception& e) {
            Core::Logger::warn(std::format("Metadati: errore fetch {}: {}", pageUrl, e.what()));
        }
        return meta;
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

        Core::Logger::info(
            std::format("{}: nextNeeded={}, html_len={}", series.name, nextNeeded, html.size()));
        if (html.empty()) {
            Core::Logger::warn(series.name + ": HTML statico non scaricabile per getCandidates");
            return results;
        }

        std::vector<EpisodeCandidate> found = parseSeriesPage(html);

        for (const auto& ep : found) {
            int local = series.continueSeries ? (ep.episodeNumber + series.passedEpisodes)
                                              : ep.episodeNumber;
            Core::Logger::info(
                std::format("{}: candidate[local={}] pageUrl={} (da data-episode-num={} e href)",
                            series.name, local, ep.episodeUrl, ep.episodeNumber));
            if (local >= nextNeeded) {
                results.push_back({local, ep.episodeUrl});
            } else {
                Core::Logger::info(std::format("{}: filtered out Ep.{} < nextNeeded={}",
                                               series.name, local, nextNeeded));
            }
        }

        Core::Logger::info(std::format("{}: final candidates={}", series.name, results.size()));
        return results;
    }

    std::vector<DownloadTask> AnimeWScraper::planSeriesTask(const Series& series,
                                                            std::atomic<bool>& stopSignal,
                                                            ScraperProgressCb progressCb) {
        std::vector<DownloadTask> results;

        auto candidates = getCandidates(series);
        Core::Logger::info(std::format("{}: getCandidates returned {} candidates", series.name,
                                       candidates.size()));
        if (candidates.empty()) {
            Core::Logger::warn(series.name + ": getCandidates empty, skipping");
            return results;
        }

        if (progressCb)
            progressCb(std::format("Analisi: {} episodi da verificare...", candidates.size()));

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
                progressCb(
                    std::format("Analisi: verifica episodio {}/{}...", checked, candidates.size()));

            std::string episodeId = extractEpisodeId(candidate.episodeUrl);
            std::string apiUrl = episodeInfoUrl() + "?id=" + episodeId + "&alt=1";

            std::string grabber;
            try {
                cpr::Response r =
                    ScraperUtils::httpGetWithRetry(cpr::Url{apiUrl}, apiHeaders, 3, 2000);
                if (r.status_code == 200 && !r.text.empty()) {
                    grabber = parseEpisodeInfo(r.text);
                } else {
                    Core::Logger::warn(std::format("{}: Ep.{} - API episodio fallita (status {})",
                                                   series.name, candidate.episodeNumber,
                                                   r.status_code));
                }
            } catch (const std::exception& e) {
                Core::Logger::warn(std::format("{}: Ep.{} - errore API episodio: {}", series.name,
                                               candidate.episodeNumber, e.what()));
            }

            if (grabber.empty()) {
                Core::Logger::warn(std::format("{}: Ep.{} - nessun URL video restituito dall'API",
                                               series.name, candidate.episodeNumber));
                continue;
            }

            Core::Logger::info(
                std::format("{}: verifica episodio completata -> [episodeNumber={}] videoUrl={}",
                            series.name, candidate.episodeNumber, grabber));

            DownloadTask task;
            task.shouldProcess = true;
            task.videoUrl = grabber;
            task.episodeNumber = candidate.episodeNumber;
            task.fileName = ScraperUtils::generateFilename(grabber, candidate.episodeNumber);
            results.push_back(task);
        }

        Core::Logger::info(std::format("{}: planSeriesTask complete, returning {} tasks",
                                       series.name, results.size()));
        return results;
    }

} // namespace Core
