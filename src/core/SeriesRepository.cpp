#include "core/SeriesRepository.hpp" // Notare "core/" all'inizio
#include <fstream>
#include <iostream>

namespace Core {

    SeriesRepository::SeriesRepository(const std::filesystem::path& jsonFilePath)
        : m_jsonFilePath(jsonFilePath) {}

    std::vector<Series> SeriesRepository::loadSeriesData() {
        if (!std::filesystem::exists(m_jsonFilePath)) {
            // Crea directory se non esiste
            if (m_jsonFilePath.has_parent_path()) {
                std::filesystem::create_directories(m_jsonFilePath.parent_path());
            }
            
            // Crea file vuoto []
            std::ofstream outFile(m_jsonFilePath);
            outFile << "[]";
            return {}; // Ritorna vettore vuoto
        }

        try {
            std::ifstream inFile(m_jsonFilePath);
            nlohmann::json jsonArray;
            inFile >> jsonArray;

            // Conversione automatica da JSON Array a std::vector<Series>
            return jsonArray.get<std::vector<Series>>();

        } catch (const std::exception& e) {
            std::cerr << "Errore caricamento dati: " << e.what() << std::endl;
            return {};
        }
    }

    void SeriesRepository::saveSeriesData(const std::vector<Series>& seriesData) {
        if (m_jsonFilePath.has_parent_path()) {
            std::filesystem::create_directories(m_jsonFilePath.parent_path());
        }

        try {
            nlohmann::json jsonArray = seriesData;
            std::ofstream outFile(m_jsonFilePath);
            outFile << jsonArray.dump(4); 
        } catch (const std::exception& e) {
            std::cerr << "Errore salvataggio dati: " << e.what() << std::endl;
        }
    }
}