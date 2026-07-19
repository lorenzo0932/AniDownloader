#pragma once
#include <string>
#include <filesystem>
#include <mutex>
#include <condition_variable>
#include <cstdio>
#include <cpr/cpr.h>

namespace Core {

    /**
     * @brief Guardiano RAII per limitare la concorrenza globale degli scraper (es. sessioni ChromeDriver).
     * Incapsula il semaforo statico condiviso tra tutti gli scraper del sistema.
     */
    class ScraperSemaphoreGuard {
    public:
        ScraperSemaphoreGuard();
        ~ScraperSemaphoreGuard();
    private:
        static std::mutex s_mutex;
        static std::condition_variable s_cv;
        static int s_activeSessions;
        static int getMaxConcurrent();
    };

    struct EpisodeInfo {
        int number = 0;
        std::string path;
    };

    class ScraperUtils {
    public:
        static std::string expandTilde(const std::string& path);
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

        // Wrapper HTTP con retry automatico per errori transiente
        // maxRetries: numero massimo di tentativi (default 3)
        // retryDelayMs: delay iniziale tra i tentativi (default 2000ms), raddoppia ad ogni tentativo
        static cpr::Response httpGetWithRetry(const cpr::Url& url, const cpr::Header& headers = {},
                                              int maxRetries = 3, int retryDelayMs = 2000);
    };
}