#include "config/AppConfigManager.hpp"
#include "config/PathHelper.hpp"
#include <fstream>
#include <iostream>
#include <thread>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <cmath>

namespace Config {
namespace fs = std::filesystem;

AppConfigManager::AppConfigManager(fs::path configPath) 
    : m_configPath(configPath) {
    try {
        fs::create_directories(PathHelper::getConfigDir());
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
        {"num_chunks", 0},              // 0 = Modalità Auto (Dinamica)
        {"auto_cleanup_on_close", true} // Pulizia file parziali
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

// --- LOGICA DI CALCOLO STRATEGIA ADATTIVA ---

ExecutionStrategy AppConfigManager::getExecutionStrategy(size_t pendingTasks, bool burstMode) const {
    ExecutionStrategy strategy;
    strategy.isBurstMode = burstMode;
    strategy.convertToH265 = get<bool>("convert_to_h265", true);
    
    int userChunks = get<int>("num_chunks", 0);

    // 1. Rilevamento Hardware (es. 32 thread)
    unsigned int totalThreads = std::thread::hardware_concurrency();
    if (totalThreads == 0) totalThreads = 4;

    // 2. Budget Thread: Burst (85% CPU) vs Background (50% CPU)
    float usageFactor = burstMode ? 0.85f : 0.50f;
    int targetThreadsBudget = static_cast<int>(totalThreads * usageFactor);

    // 3. Parallelismo Video (Tasks)
    if (burstMode) {
        // Se ci sono più video, ne processiamo 2 contemporaneamente per saturare l'I/O
        strategy.maxConcurrentTasks = (pendingTasks > 1) ? 2 : 1;
    } else {
        // In background sempre 1 solo video alla volta
        strategy.maxConcurrentTasks = 1;
    }

    // 4. Calcolo Dinamico Chunking (Logica "Sweet Spot")
    if (userChunks == 0) {
        // Quanti thread abbiamo a disposizione per ogni video nel budget?
        float threadsAllocatedPerVideo = (float)targetThreadsBudget / strategy.maxConcurrentTasks;

        /* 
           x265 lavora meglio con 6-8 thread per istanza. 
           Dividiamo i thread disponibili per il numero ideale di thread per processo (6.5)
           per ottenere il numero di chunk necessari a saturare la CPU.
        */
        int calculatedChunks = std::round(threadsAllocatedPerVideo / 6.5f);
        
        // Limiti: min 1, max 8 (per non frammentare eccessivamente i file)
        strategy.chunksPerTask = std::clamp(calculatedChunks, 1, 8);
    } else {
        // L'utente ha forzato un valore (es. 1, 2, 4...)
        strategy.chunksPerTask = userChunks;
    }

    // 5. Calcolo finale threads per ogni comando FFmpeg
    int totalProcesses = strategy.maxConcurrentTasks * strategy.chunksPerTask;
    int t = targetThreadsBudget / totalProcesses;
    
    // Assegna almeno 1 thread, max 12 (oltre i 12 x265 scala male)
    strategy.threadsPerFFmpeg = std::clamp(t, 1, 12);

    return strategy;
}

void AppConfigManager::logFinalResult(const std::string& seriesName, double dlTime, double convTime) const {
    try {
        fs::path logPath = get<std::string>("log_file_path", PathHelper::getLogFilePath().string());
        std::ofstream logFile(logPath, std::ios::app);
        
        if (logFile.is_open()) {
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            logFile << "[" << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << "] "
                    << std::left << std::setw(45) << seriesName 
                    << " | DL: " << std::fixed << std::setprecision(2) << std::setw(8) << dlTime << "s"
                    << " | Conv: " << std::setw(8) << convTime << "s" << std::endl;
        }
    } catch (...) {}
}

} // namespace Config