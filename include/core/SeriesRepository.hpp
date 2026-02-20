#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include "Series.hpp" // Includiamo la nuova struct

namespace Core {

    class SeriesRepository {
    public:
        explicit SeriesRepository(const std::filesystem::path& jsonFilePath);

        // Ora restituisce un vettore tipizzato di Serie
        std::vector<Series> loadSeriesData();

        // Ora prende un vettore tipizzato
        void saveSeriesData(const std::vector<Series>& seriesData);

    private:
        std::filesystem::path m_jsonFilePath;
    };

}