#include "config/AppConfigManager.hpp"
#include "config/PathHelper.hpp"
#include "core/Logger.hpp"
#include <fstream>
#include <thread>
#include <algorithm>
#include <cmath>

namespace Config {
namespace fs = std::filesystem;

AppConfigManager::AppConfigManager(fs::path configPath) 
    : m_configPath(configPath) {
    try {
        fs::create_directories(PathHelper::getConfigDir());
    } catch (const std::exception& e) {
        Core::Logger::error("Impossibile creare cartella config: " + std::string(e.what()));
    }
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
        {"auto_cleanup_on_close", true}, // Pulizia file parziali
        {"max_network_retries", 3},      // Retry HTTP/aria2c
        {"retry_delay_ms", 2000},        // Delay iniziale tra retry (exponential backoff)
        {"chromedriver_path", "chromedriver"} // Path per ChromeDriver/ChromeDriver.exe
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
    } catch (const std::exception& e) {
        Core::Logger::error("Errore parsing config JSON: " + std::string(e.what()));
        m_config = getDefaultConfig();
        saveConfig(m_config);
    }
}

void AppConfigManager::saveConfig(const nlohmann::json& configData) {
    try {
        fs::create_directories(m_configPath.parent_path());
        std::ofstream f(m_configPath);
        f << configData.dump(4);
    } catch (const std::exception& e) {
        Core::Logger::error("Impossibile salvare config: " + std::string(e.what()));
    }
}

void AppConfigManager::set(const std::string& key, const nlohmann::json& value) {
    m_config[key] = value;
    saveConfig(m_config);
}

nlohmann::json AppConfigManager::getAll() const { return m_config; }
void AppConfigManager::reloadConfig() { loadConfig(); }

int AppConfigManager::getMaxNetworkRetries() const { return get<int>("max_network_retries", 3); }
int AppConfigManager::getRetryDelayMs() const { return get<int>("retry_delay_ms", 2000); }

// --- LOGICA DI CALCOLO STRATEGIA ADATTIVA ---

ExecutionStrategy AppConfigManager::getExecutionStrategy(size_t pendingTasks, bool burstMode) const {
    ExecutionStrategy strategy;
    strategy.isBurstMode = burstMode;
    strategy.convertToH265 = get<bool>("convert_to_h265", true);
    
    int userChunks = get<int>("num_chunks", 0);

    // 1. Rilevamento Hardware reale
    unsigned int totalThreads = std::thread::hardware_concurrency();
    if (totalThreads == 0) totalThreads = 4;

    // 2. Budget Thread: Burst (85% CPU) vs Background (50% CPU)
    float usageFactor = burstMode ? 0.85f : 0.50f;
    int targetThreadsBudget = static_cast<int>(totalThreads * usageFactor);
    if (targetThreadsBudget < 1) targetThreadsBudget = 1;

    // --- FIX DI SICUREZZA 1: SALVAGENTE PER MACCHINE LOW-END (< 6 Thread) ---
    if (targetThreadsBudget < 6) {
        strategy.maxConcurrentTasks = 1;
        strategy.chunksPerTask = 1;
        strategy.threadsPerFFmpeg = targetThreadsBudget;
        return strategy;
    }

    // --- FIX DI SICUREZZA 2: LIMITATORE DI CONCORRENZA PER COERENZA DELLA CACHE (CCD) ---
    // Evita che troppi file paralleli intasino l'I/O del disco e causino cache thrashing sui CCD.
    int maxAllowedConcurrent = 2; // Default ottimo per PC consumer (Ryzen 5950X / i9)
    if (totalThreads > 128) {
        maxAllowedConcurrent = 12; // Per Server giganti (Xeon / EPYC)
    } else if (totalThreads > 64) {
        maxAllowedConcurrent = 6;  // Per workstation Threadripper
    } else if (totalThreads > 32) {
        maxAllowedConcurrent = 4;  // Per CPU high-end desktop
    }

    // --- LOGICA ADATTIVA FLUIDA ---
    if (userChunks == 0) {
        // Modalità Auto (Dinamica)
        if (pendingTasks <= 1) {
            // Un solo file in coda: concentriamo tutta la CPU su di esso
            strategy.maxConcurrentTasks = 1;
            strategy.chunksPerTask = std::clamp(targetThreadsBudget / 6, 1, 8);
        } 
        else {
            // Più file in coda: calcolo della concorrenza ideale tramite radice quadrata
            int idealConcurrent = static_cast<int>(std::sqrt(targetThreadsBudget));
            if (idealConcurrent < 1) idealConcurrent = 1;
            
            // Applica il limite di sicurezza basato sulla cache/disco
            idealConcurrent = std::min(idealConcurrent, maxAllowedConcurrent);
            
            // Imposta la concorrenza reale limitata dai task effettivamente pendenti
            strategy.maxConcurrentTasks = std::min(idealConcurrent, static_cast<int>(pendingTasks));
            
            // Dividiamo i thread allocati per ogni file in chunk da ~6 thread ciascuno
            int threadsPerFile = targetThreadsBudget / strategy.maxConcurrentTasks;
            strategy.chunksPerTask = std::clamp(threadsPerFile / 6, 1, 4);
        }
    } 
    else {
        // L'utente ha forzato un valore fisso per i chunk
        strategy.chunksPerTask = userChunks;
        
        int idealConcurrent = targetThreadsBudget / (strategy.chunksPerTask * 6);
        idealConcurrent = std::min(idealConcurrent, maxAllowedConcurrent);
        if (idealConcurrent < 1) idealConcurrent = 1;
        strategy.maxConcurrentTasks = std::min(idealConcurrent, static_cast<int>(pendingTasks));
    }

    // 5. Calcolo finale dei thread effettivi per singolo comando FFmpeg
    int totalProcesses = strategy.maxConcurrentTasks * strategy.chunksPerTask;
    int t = targetThreadsBudget / totalProcesses;
    
    // Assegna almeno 1 thread, max 12 (oltre i 12 x265 scala male)
    strategy.threadsPerFFmpeg = std::clamp(t, 1, 12);

    return strategy;
}


} // namespace Config