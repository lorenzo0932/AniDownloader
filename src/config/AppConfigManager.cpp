#include "config/AppConfigManager.hpp"
#include "config/PathHelper.hpp"
#include <fstream>
#include <iostream>

namespace Config {
namespace fs = std::filesystem;

AppConfigManager::AppConfigManager(fs::path configPath) 
    : m_configPath(configPath) {
    try {
        fs::create_directories(PathHelper::getConfigDir());
        fs::create_directories(PathHelper::getLogFilePath().parent_path());
    } catch (...) {}
    loadConfig();
}

nlohmann::json AppConfigManager::getDefaultConfig() {
    return {
        {"json_file_path", PathHelper::getSeriesJsonPath().string()},
        {"output_dir", PathHelper::getVideosDir().string()},
        {"log_file_path", PathHelper::getLogFilePath().string()},
        {"is_json_path_customized", false},
        {"convert_to_h265", true},
        {"num_chunks", 1}
    };
}

void AppConfigManager::loadConfig() {
    if (!fs::exists(m_configPath)) {
        m_config = getDefaultConfig();
        saveConfig(m_config);
        return;
    }
    try {
        std::ifstream f(m_configPath);
        m_config = nlohmann::json::parse(f);
        nlohmann::json defaults = getDefaultConfig();
        for (auto& [key, value] : defaults.items()) {
            if (!m_config.contains(key)) m_config[key] = value;
        }
    } catch (...) {
        m_config = getDefaultConfig();
        saveConfig(m_config);
    }
}

void AppConfigManager::saveConfig(const nlohmann::json& configData) {
    try {
        fs::create_directories(m_configPath.parent_path());
        std::ofstream f(m_configPath);
        f << configData.dump(4);
    } catch (...) {}
}

void AppConfigManager::set(const std::string& key, const nlohmann::json& value) {
    m_config[key] = value;
    saveConfig(m_config);
}

nlohmann::json AppConfigManager::getAll() const { return m_config; }
}