#pragma once
#include <vector>
#include <filesystem>
#include <optional>
#include <mutex>
#include "Series.hpp"

namespace Core {
    class SeriesRepository {
    public:
        explicit SeriesRepository(const std::filesystem::path& jsonFilePath);
        const std::vector<Series>& loadSeriesData(bool forceReload = false);
        void saveSeriesData(const std::vector<Series>& seriesData);
        void invalidateCache();

    private:
        std::filesystem::path m_jsonFilePath;
        std::optional<std::vector<Series>> m_cache; // Re-inserito optional
        mutable std::mutex m_mutex; 
    };
}