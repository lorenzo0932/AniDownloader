#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>
#include "config/PathHelper.hpp" // Fondamentale

namespace Config {
    class AppConfigManager {
    public:
        // Qui usiamo il nuovo PathHelper invece della vecchia costante
        explicit AppConfigManager(std::filesystem::path configPath = PathHelper::getAppConfigPath());

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

    private:
        std::filesystem::path m_configPath;
        nlohmann::json m_config;
        void loadConfig();
        void saveConfig(const nlohmann::json& configData);
        nlohmann::json getDefaultConfig();
    };
}