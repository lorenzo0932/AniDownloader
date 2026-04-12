#include "core/SeriesRepository.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp> // Assicurati che sia incluso

namespace Core {

    SeriesRepository::SeriesRepository(const std::filesystem::path& jsonFilePath)
        : m_jsonFilePath(jsonFilePath) {}

    const std::vector<Series>& SeriesRepository::loadSeriesData(bool forceReload) {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!forceReload && m_cache.has_value()) {
            return m_cache.value();
        }

        if (!std::filesystem::exists(m_jsonFilePath)) {
            if (m_jsonFilePath.has_parent_path()) {
                std::filesystem::create_directories(m_jsonFilePath.parent_path());
            }
            std::ofstream outFile(m_jsonFilePath);
            outFile << "[]";
            m_cache = std::vector<Series>{};
            return m_cache.value();
        }

        try {
            std::ifstream inFile(m_jsonFilePath);
            nlohmann::json jsonArray;
            inFile >> jsonArray;
            m_cache = jsonArray.get<std::vector<Series>>();
        } catch (const std::exception& e) {
            std::cerr << "Errore caricamento dati: " << e.what() << std::endl;
            static const std::vector<Series> emptyFallback;
            return emptyFallback;
        }
        return m_cache.value();
    }

    void SeriesRepository::saveSeriesData(const std::vector<Series>& seriesData) {
        std::lock_guard<std::mutex> lock(m_mutex);

        try {
            nlohmann::json jsonArray = seriesData;
            if (m_jsonFilePath.has_parent_path()) {
                std::filesystem::create_directories(m_jsonFilePath.parent_path());
            }
            std::ofstream outFile(m_jsonFilePath);
            if (outFile.is_open()) {
                outFile << jsonArray.dump(4);
                m_cache = seriesData; // Aggiorna la cache solo dopo il successo
            }
        } catch (const std::exception& e) {
            std::cerr << "Errore salvataggio dati: " << e.what() << std::endl;
        }
    }

    void SeriesRepository::invalidateCache() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cache.reset();
    }
}