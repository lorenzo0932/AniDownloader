#pragma once
#include <cpr/cpr.h>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

    struct EpisodeInfo {
        int number = 0;
        std::string path;
    };

    // Coppia (numero episodio, URL pagina episodio) estratta dal parsing della
    // pagina serie. Condivisa dagli scraper (AnimeW e AnimeU).
    struct EpisodeCandidate {
        int episodeNumber;
        std::string episodeUrl;
    };

    class ScraperUtils {
      public:
        static std::string expandTilde(std::string_view path);
        static EpisodeInfo getHighestEpisodeFile(const std::string& seriesPath);
        static int getNextEpisodeNum(const std::string& seriesPath);
        static std::string generateFilename(const std::string& downloadUrl, int episodeNumber);

        // Quoting dei percorsi per shell, cross-platform
        // Windows: doppio apice con escape " -> \"
        // POSIX:   singolo apice con escape ' -> '\'
        static std::string Q(const std::string& path);

        // Compatibilità popen/pclose cross-platform (Windows usa _popen/_pclose)
        static FILE* popenCompat(const std::string& cmd, const char* mode);
        static int pcloseCompat(FILE* fp);

        // Redirezioni per output silenziato, cross-platform
        static inline std::string DEVNULL() {
#ifdef _WIN32
            return " >NUL 2>NUL";
#else
            return " > /dev/null 2>&1";
#endif
        }

        static inline std::string REDIR_STDERR() {
#ifdef _WIN32
            return " 2>NUL";
#else
            return " 2>/dev/null";
#endif
        }

        // User-Agent che rileva la piattaforma (Windows/Linux)
        static std::string platformUserAgent();

        // Estrae il nome della serie dalla pagina web dato l'URL
        static std::string fetchSeriesNameFromUrl(const std::string& url);

        // Estrae il titolo della serie dall'HTML della pagina (rimuove prefissi
        // "AnimeWorld - " e suffissi "Episodio N"). Funzione pura, testabile
        // senza rete.
        static std::string extractSeriesTitle(const std::string& html);

        // Scansione directory: restituisce {epNumber → fullPath} per file >1MB che matchano Ep[N]
        static std::map<int, std::string> scanEpisodesMap(const std::string& seriesPath);

        // Verifica se epNum esiste nella mappa ed è valido (ffprobe). Torna path o ""
        static std::string validEpisodePath(const std::map<int, std::string>& episodesMap,
                                            int epNum);

        // Logica centrale: calcola il prossimo episodio da scaricare
        // Se lastDownloadedEpisode <= 0 → fallback a getHighestEpisodeFile()
        // Se lastDownloadedEpisode > 0 → cerca backward da N in giù, torna il primo valido + 1
        // Se nessun valido → torna 1
        static int computeNextNeeded(const std::string& seriesPath, int lastDownloadedEpisode,
                                     const std::map<int, std::string>& episodesMap);

        // Wrapper HTTP con retry automatico per errori transiente
        // maxRetries: numero massimo di tentativi (default 3)
        // retryDelayMs: delay iniziale tra i tentativi (default 2000ms), raddoppia ad ogni
        // tentativo
        static cpr::Response httpGetWithRetry(const cpr::Url& url, const cpr::Header& headers = {},
                                              int maxRetries = 3, int retryDelayMs = 2000);
    };
} // namespace Core