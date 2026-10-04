#pragma once
#include "Series.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

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
        // Snapshot del file al momento dell'ultimo load/save riuscito: se a
        // una load senza force lo stat differisce, il file è cambiato fuori
        // dal processo (es. altro daemon) e la cache va ricaricata.
        std::filesystem::file_time_type m_lastWrite{};
        std::uintmax_t m_lastSize{0};
        bool m_haveStat{false};
    };
} // namespace Core