#pragma once
#include <vector>
#include <filesystem>
#include <optional>
#include <mutex>
#include <map>
#include "Series.hpp"

namespace Core {
    class SeriesRepository {
    public:
        explicit SeriesRepository(const std::filesystem::path& jsonFilePath);
        const std::vector<Series>& loadSeriesData(bool forceReload = false);
        void saveSeriesData(const std::vector<Series>& seriesData);
        void invalidateCache();

        // Aggiorna lastDownloadedEpisode/lastDownloadedAt per le serie in maxEpisodes
        // (una sola scrittura, dopo che tutti i download sono terminati).
        // Ritorna true se almeno una serie è stata aggiornata.
        bool applyDownloadedEpisodes(const std::map<std::string, int>& maxEpisodes,
                                     const std::string& timestamp);

    private:
        std::filesystem::path m_jsonFilePath;
        std::optional<std::vector<Series>> m_cache; // Re-inserito optional
        mutable std::mutex m_mutex; 
    };
}