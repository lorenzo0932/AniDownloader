#pragma once
#include "config/PathHelper.hpp"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace Config {

    /**
     * @brief Parametri calcolati dinamicamente per bilanciare velocità e reattività del sistema.
     */
    struct ExecutionStrategy {
        int maxConcurrentTasks; // Quanti video scaricare/convertire insieme
        int chunksPerTask; // Numero di segmenti in cui dividere il video (0 o 1 = codifica diretta)
        int threadsPerFFmpeg; // Numero di thread assegnati a ogni processo FFmpeg
        bool convertToH265;   // Flag per abilitare/disabilitare la conversione
        bool isBurstMode;     // true = Massima potenza (Burst), false = Background (nice -n 15)
    };

    class AppConfigManager {
      public:
        explicit AppConfigManager(
            const std::filesystem::path& configPath = PathHelper::getAppConfigPath());

        template <typename T> T get(const std::string& key, T defaultValue) const {
            if (m_config.contains(key)) {
                try {
                    return m_config.at(key).get<T>();
                } catch (...) {
                    return defaultValue;
                }
            }
            return defaultValue;
        }

        void set(const std::string& key, const nlohmann::json& value);
        nlohmann::json getAll() const;
        void reloadConfig();

        int getMaxNetworkRetries() const;
        int getRetryDelayMs() const;

        /**
         * @brief Genera la strategia ottimale basata sulla CPU rilevata e sulle impostazioni
         * utente. Applica la logica "Sweet Spot" per x265 (6-8 thread per processo).
         */
        ExecutionStrategy getExecutionStrategy(size_t pendingTasks, bool burstMode) const;

      private:
        std::filesystem::path m_configPath;
        nlohmann::json m_config;

        void loadConfig();
        void saveConfig(const nlohmann::json& configData);
        nlohmann::json getDefaultConfig();
    };
} // namespace Config