#include "config/AppConfigManager.hpp"
#include "config/PathHelper.hpp"
#include <fstream>
#include <iostream>
#include <thread>
#include <algorithm>
#include <chrono>
#include <iomanip>

namespace Config {
namespace fs = std::filesystem;

AppConfigManager::AppConfigManager(fs::path configPath) 
    : m_configPath(configPath) {
    try {
        fs::create_directories(PathHelper::getConfigDir());
        // Assicuriamoci che la cartella che conterrà il log esista
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

// --- IMPLEMENTAZIONE STRATEGIA ADATTIVA ---

ExecutionStrategy AppConfigManager::getExecutionStrategy(size_t pendingTasks, bool burstMode) const {
    ExecutionStrategy strategy;
    strategy.isBurstMode = burstMode;
    strategy.convertToH265 = get<bool>("convert_to_h265", true);

    // Rilevamento core logici (es. 32 sul tuo 5950X)
    unsigned int totalThreads = std::thread::hardware_concurrency();
    if (totalThreads == 0) totalThreads = 4; // Fallback

    // Carico target: Burst (85%) o Background (50%)
    float usageFactor = burstMode ? 0.85f : 0.50f;
    unsigned int targetThreads = static_cast<unsigned int>(totalThreads * usageFactor);

    if (burstMode) {
        // Modalità Performance: prioritizziamo parallelismo video e chunking
        strategy.maxConcurrentTasks = (pendingTasks > 1) ? 2 : 1;
        strategy.chunksPerTask = (pendingTasks <= 2) ? 4 : 2;
    } else {
        // Modalità Background: un solo video, chunking ridotto per non saturare l'I/O
        strategy.maxConcurrentTasks = 1;
        strategy.chunksPerTask = (totalThreads > 16) ? 2 : 1;
    }

    // Calcolo thread per istanza FFmpeg
    // Esempio: 27 target / (2 video * 2 chunk) = 6.75 -> 6 thread per processo
    int calculatedThreads = targetThreads / (strategy.maxConcurrentTasks * strategy.chunksPerTask);
    
    // Clamp per efficienza: x265 raramente beneficia di più di 12 thread per frame
    strategy.threadsPerFFmpeg = std::clamp(calculatedThreads, 1, 12);

    return strategy;
}

void AppConfigManager::logFinalResult(const std::string& seriesName, double dlTime, double convTime) const {
    try {
        // Recuperiamo il path dal config o usiamo il default dal PathHelper
        fs::path logPath = get<std::string>("log_file_path", PathHelper::getLogFilePath().string());
        std::ofstream logFile(logPath, std::ios::app);
        
        if (logFile.is_open()) {
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            logFile << "[" << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << "] "
                    << std::left << std::setw(45) << seriesName 
                    << " | DL: " << std::fixed << std::setprecision(2) << std::setw(8) << dlTime << "s"
                    << " | Conv: " << std::setw(8) << convTime << "s" << std::endl;
        }
    } catch (...) {
        // Silenzioso, ma potresti loggare su stderr se necessario
    }
}

} // namespace Config