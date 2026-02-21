#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>
#include "config/PathHelper.hpp"

namespace Config {

    /**
     * @brief Definisce i parametri di esecuzione calcolati dinamicamente.
     */
    struct ExecutionStrategy {
        int maxConcurrentTasks; // Video processati insieme
        int chunksPerTask;      // Numero di chunk per ogni video
        int threadsPerFFmpeg;   // -threads per ogni istanza FFmpeg
        bool convertToH265;     // Se eseguire la conversione
        bool isBurstMode;       // Modalità Burst vs Silent (Background)
    };

    class AppConfigManager {
    public:
        // Usa il PathHelper per il default come nel tuo originale
        explicit AppConfigManager(std::filesystem::path configPath = PathHelper::getAppConfigPath());

        // Metodo template originale per recupero flessibile
        template<typename T>
        T get(const std::string& key, T defaultValue) const {
            if (m_config.contains(key)) {
                try {
                    return m_config.at(key).get<T>();
                } catch (...) { return defaultValue; }
            }
            return defaultValue;
        }

        void set(const std::string& key, const nlohmann::json& value);
        nlohmann::json getAll() const;

        /**
         * @brief Genera la strategia basata sulla CPU e il numero di task pendenti.
         */
        ExecutionStrategy getExecutionStrategy(size_t pendingTasks, bool burstMode) const;

        /**
         * @brief Registra nel file di storico i tempi di esecuzione di una serie.
         */
        void logFinalResult(const std::string& seriesName, double dlTime, double convTime) const;

    private:
        std::filesystem::path m_configPath;
        nlohmann::json m_config;
        
        void loadConfig();
        void saveConfig(const nlohmann::json& configData);
        nlohmann::json getDefaultConfig();
    };
}