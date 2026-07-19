#pragma once
#include <string>
#include <filesystem>
#include <mutex>
#include <condition_variable>

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

    class ScraperUtils {
    public:
        static std::string expandTilde(const std::string& path);
        static int getNextEpisodeNum(const std::string& seriesPath);
        static std::string generateFilename(const std::string& downloadUrl, int episodeNumber);
    };
}