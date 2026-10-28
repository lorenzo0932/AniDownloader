#include "core/SeriesIdentity.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace Core {

    namespace {

        std::string trim(const std::string& s) {
            size_t a = 0;
            while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
                ++a;
            size_t b = s.size();
            while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
                --b;
            return s.substr(a, b - a);
        }

        std::string lowerAscii(std::string s) {
            for (auto& c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        std::string stripFragment(const std::string& s) {
            auto pos = s.find('#');
            return (pos == std::string::npos) ? s : s.substr(0, pos);
        }

        std::string stripQuery(const std::string& s) {
            auto pos = s.find('?');
            return (pos == std::string::npos) ? s : s.substr(0, pos);
        }

        std::string stripTrailingSlash(const std::string& path) {
            if (path.size() > 1 && !path.empty() && path.back() == '/')
                return path.substr(0, path.size() - 1);
            return path;
        }

        bool isAnimeWService(const std::string& service) { return service == "animeW_scraper"; }

        bool isAnimeUService(const std::string& service) { return service == "animeU_scraper"; }

        bool isAnimeWHost(const std::string& hostLower) {
            return hostLower == "www.animeworld.ac" || hostLower == "animeworld.ac" ||
                   hostLower == "www.animeworld.so" || hostLower == "animeworld.so";
        }

        bool isAnimeUHost(const std::string& hostLower) {
            return hostLower == "animeunity.to" || hostLower == "www.animeunity.to" ||
                   hostLower == "animeunity.so" || hostLower == "www.animeunity.so";
        }

        // Divide il path in segmenti non vuoti (senza query/fragment).
        std::vector<std::string> splitPath(const std::string& path) {
            std::vector<std::string> out;
            std::string cur;
            for (char c : path) {
                if (c == '/') {
                    if (!cur.empty()) {
                        out.push_back(cur);
                        cur.clear();
                    }
                } else {
                    cur += c;
                }
            }
            if (!cur.empty())
                out.push_back(cur);
            return out;
        }

    } // namespace

    std::string discoverIdentity(const std::string& pageUrl) {
        static const std::string kBase = "https://www.animeworld.ac";
        std::string u = trim(pageUrl);
        if (u.empty())
            return "";
        if (u[0] == '/')
            return seriesIdentity("animeW_scraper", kBase + u);
        return seriesIdentity("animeW_scraper", u);
    }

    std::string seriesIdentity(const std::string& service, const std::string& url) {
        std::string svc = trim(service);
        std::string u = trim(stripFragment(url));
        if (svc.empty() || u.empty())
            return "";

        const bool isAW = isAnimeWService(svc);
        const bool isAU = isAnimeUService(svc);
        if (!isAW && !isAU) {
            // Servizio sconosciuto: fallback conservativo, mai merge.
            return svc + "|url:" + u;
        }

        // Relativo del catalogo: solo AW lo usa (/play/...).
        if (!u.empty() && u[0] == '/') {
            if (isAW) {
                const std::string abs = std::string("https://www.animeworld.ac") + u;
                return seriesIdentity(svc, abs);
            }
            return svc + "|relative:" + stripTrailingSlash(stripQuery(u));
        }

        // Assoluto: scheme://host/path...
        auto schemePos = u.find("://");
        if (schemePos == std::string::npos)
            return svc + "|url:" + u;
        std::string after = u.substr(schemePos + 3);
        if (after.empty())
            return svc + "|url:" + u;

        // Host fino a '/', '?' o '#'; porta esplicita = non canonico (fallback).
        size_t hostEnd = after.find_first_of("/?#");
        std::string hostPort = (hostEnd == std::string::npos) ? after : after.substr(0, hostEnd);
        std::string rest = (hostEnd == std::string::npos) ? "/" : after.substr(hostEnd);
        if (hostPort.find(':') != std::string::npos) {
            // Host con porta (es. fixture locali/smoke): mai collassare sul
            // canonico, ma identità comunque stabile e deterministica.
            std::string p = stripTrailingSlash(stripQuery(stripFragment(rest)));
            if (p.empty())
                p = "/";
            return svc + "|url:" + lowerAscii(hostPort) + p;
        }
        const std::string hostLower = lowerAscii(hostPort);

        std::string path = stripTrailingSlash(stripQuery(stripFragment(rest)));
        if (path.empty())
            path = "/";

        if (isAW) {
            if (!isAnimeWHost(hostLower))
                return svc + "|url:" + hostLower + path;
            // Solo la pagina SERIE /play/<slug.ID> ha identità canonica.
            // Le pagine episodio (/play/<slug.ID>/<ep>) restano distinte:
            // mai derivare l'identità della serie da un URL episodio.
            auto segs = splitPath(path);
            if (segs.size() == 2 && segs[0] == "play" && !segs[1].empty())
                return svc + ":/play/" + segs[1];
            return svc + "|url:www.animeworld.ac" + path;
        }

        // AnimeU: il formato serie non è documentato quanto AW
        // (episodi /watch/... in fixture), quindi niente estrazione slug:
        // identità = servizio + path completo normalizzato sul canonico.
        if (!isAnimeUHost(hostLower))
            return svc + "|url:" + hostLower + path;
        return svc + ":" + path;
    }

} // namespace Core
