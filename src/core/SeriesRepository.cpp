#include "core/SeriesRepository.hpp"
#include "core/Logger.hpp"
#include <fstream>
#include <nlohmann/json.hpp>

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
            Logger::error("Errore caricamento dati: " + std::string(e.what()));
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
            Logger::error("Errore salvataggio dati: " + std::string(e.what()));
        }
    }

    void SeriesRepository::invalidateCache() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cache.reset();
    }

    bool SeriesRepository::applyDownloadedEpisodes(const std::map<std::string, int>& maxEpisodes,
                                                   const std::string& timestamp) {
        if (maxEpisodes.empty()) return false;

        // loadSeriesData gestisce il lock: unico punto di accesso alla cache.
        // Copia necessaria: la cache è esposta come const&.
        auto series = loadSeriesData();

        bool updated = false;
        for (auto& s : series) {
            auto it = maxEpisodes.find(s.name);
            if (it == maxEpisodes.end()) continue;
            s.lastDownloadedAt = timestamp;
            if (it->second > s.lastDownloadedEpisode) {
                s.lastDownloadedEpisode = it->second;
            }
            updated = true;
        }

        if (updated) {
            saveSeriesData(series);
        }
        return updated;
    }
}