#include "core/SeriesRepository.hpp"
#include "core/Logger.hpp"
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace Core {

namespace {

// Scrittura JSON atomica (tmp nella stessa dir + rename; fallback copy su
// Windows dove rename fallisce con target esistente). Su eccezione rimuove il
// tmp: il chiamante decide se loggare.
void writeJsonAtomic(const std::filesystem::path& target, const nlohmann::json& data, int indent) {
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path());
    }
    auto tmpPath = target;
    tmpPath += ".tmp";
    {
        std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) throw std::runtime_error("Impossibile aprire " + tmpPath.string());
        f << data.dump(indent);
    }
    std::error_code ec;
    std::filesystem::rename(tmpPath, target, ec);
    if (ec) {
        std::filesystem::copy_file(tmpPath, target, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) throw std::runtime_error("copy fallito: " + ec.message());
        std::filesystem::remove(tmpPath, ec);
    }
}

}

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
            // Scrittura atomica (tmp + rename): evita series_data.json corrotto
            // su crash a metà scrittura.
            writeJsonAtomic(m_jsonFilePath, jsonArray, 4);
            m_cache = seriesData; // Aggiorna la cache solo dopo il successo
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
            // Semantica: episodio e timestamp si aggiornano SOLO con avanzamento
            // reale. Conversioni locali di manutenzione o episodi non più alti
            // non devono "sporcare" il timestamp né scrivere il file.
            if (it->second > s.lastDownloadedEpisode) {
                s.lastDownloadedEpisode = it->second;
                s.lastDownloadedAt = timestamp;
                updated = true;
            }
        }

        if (updated) {
            saveSeriesData(series);
        }
        return updated;
    }
}