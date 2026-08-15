#include "config/AppConfigManager.hpp"
#include "config/PathHelper.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace Config {
    namespace fs = std::filesystem;

    namespace {

        // Scrittura JSON atomica condivisa dai writer di config.
        // 1. Serializza e scrive su <target>.tmp nella stessa directory (stesso fs).
        // 2. rename(tmp, target): atomico su POSIX.
        // 3. Se rename fallisce (Windows con target esistente): copy_file + rimozione tmp.
        // 4. Su eccezione: rimozione del tmp (il chiamante logga).
        void writeJsonAtomic(const std::filesystem::path& target, const nlohmann::json& data,
                             int indent) {
            fs::create_directories(target.parent_path());
            auto tmpPath = target;
            tmpPath += ".tmp";
            {
                std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
                if (!f.is_open())
                    throw std::runtime_error("Impossibile aprire " + tmpPath.string());
                f << data.dump(indent);
            }
            std::error_code ec;
            fs::rename(tmpPath, target, ec);
            if (ec) {
                fs::copy_file(tmpPath, target, fs::copy_options::overwrite_existing, ec);
                if (ec)
                    throw std::runtime_error("copy fallito: " + ec.message());
                fs::remove(tmpPath, ec);
            }
        }

    } // namespace

    AppConfigManager::AppConfigManager(fs::path configPath) : m_configPath(configPath) {
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
            {"num_chunks", 0},               // 0 = Modalità Auto (Dinamica)
            {"auto_cleanup_on_close", true}, // Pulizia file parziali (deprecata: vince resume_interrupted_downloads)
            {"resume_interrupted_downloads", true}, // Feature 11: riprende i download interrotti tra run
            {"max_network_retries", 3},      // Retry HTTP/aria2c
            {"retry_delay_ms", 2000}         // Delay iniziale tra retry (exponential backoff)
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
            // Feature 11 — matrice di migrazione di resume_interrupted_downloads
            // (prima del merge dei default: `contains` deve riflettere il FILE):
            // | auto_cleanup_on_close      | resume_interrupted_downloads | Effetto |
            // | assente (default storico)  | assente → default true       | partials trattenuti (cambio di default = scopo feature 11) |
            // | true esplicito             | assente                      | comportamento vecchio preservato: resume=false |
            // | false esplicito            | assente                      | già tratteneva i partials ⟺ resume=true, nessun cambio |
            // | qualunque                  | presente                     | la nuova chiave vince |
            if (!m_config.contains("resume_interrupted_downloads") &&
                m_config.contains("auto_cleanup_on_close") &&
                m_config["auto_cleanup_on_close"] == true) {
                m_config["resume_interrupted_downloads"] = false;
            }
            nlohmann::json defaults = getDefaultConfig();
            for (auto& [key, value] : defaults.items()) {
                if (!m_config.contains(key))
                    m_config[key] = value;
            }
        } catch (const std::exception& e) {
            Core::Logger::error("Errore parsing config JSON: " + std::string(e.what()));
            m_config = getDefaultConfig();
            saveConfig(m_config);
        }
    }

    void AppConfigManager::saveConfig(const nlohmann::json& configData) {
        try {
            // Scrittura atomica: tmp nella stessa directory + rename (atomico su
            // POSIX). Evita file config.json corrotto su crash a metà scrittura.
            writeJsonAtomic(m_configPath, configData, 4);
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

    int AppConfigManager::getMaxNetworkRetries() const {
        return get<int>("max_network_retries", 3);
    }
    int AppConfigManager::getRetryDelayMs() const { return get<int>("retry_delay_ms", 2000); }

    // --- LOGICA DI CALCOLO STRATEGIA ADATTIVA ---

    ExecutionStrategy AppConfigManager::getExecutionStrategy(size_t pendingTasks,
                                                             bool burstMode) const {
        ExecutionStrategy strategy;
        strategy.isBurstMode = burstMode;
        strategy.convertToH265 = get<bool>("convert_to_h265", true);

        int userChunks = get<int>("num_chunks", 0);

        // 1. Rilevamento Hardware reale
        unsigned int totalThreads = std::thread::hardware_concurrency();
        if (totalThreads == 0)
            totalThreads = 4;

        // 2. Budget Thread: Burst (85% CPU) vs Background (50% CPU)
        float usageFactor = burstMode ? 0.85f : 0.50f;
        int targetThreadsBudget = static_cast<int>(totalThreads * usageFactor);
        if (targetThreadsBudget < 1)
            targetThreadsBudget = 1;

        // --- FIX DI SICUREZZA 1: SALVAGENTE PER MACCHINE LOW-END (< 6 Thread) ---
        if (targetThreadsBudget < 6) {
            strategy.maxConcurrentTasks = 1;
            strategy.chunksPerTask = 1;
            strategy.threadsPerFFmpeg = targetThreadsBudget;
            return strategy;
        }

        // --- FIX DI SICUREZZA 2: LIMITATORE DI CONCORRENZA PER COERENZA DELLA CACHE (CCD) ---
        // Evita che troppi file paralleli intasino l'I/O del disco e causino cache thrashing sui
        // CCD.
        int maxAllowedConcurrent = 2; // Default ottimo per PC consumer (Ryzen 5950X / i9)
        if (totalThreads > 128) {
            maxAllowedConcurrent = 12; // Per Server giganti (Xeon / EPYC)
        } else if (totalThreads > 64) {
            maxAllowedConcurrent = 6; // Per workstation Threadripper
        } else if (totalThreads > 32) {
            maxAllowedConcurrent = 4; // Per CPU high-end desktop
        }

        // --- LOGICA ADATTIVA FLUIDA ---
        if (userChunks == 0) {
            // Modalità Auto (Dinamica)
            if (pendingTasks <= 1) {
                // Un solo file in coda: concentriamo tutta la CPU su di esso
                strategy.maxConcurrentTasks = 1;
                strategy.chunksPerTask = std::clamp(targetThreadsBudget / 6, 1, 8);
            } else {
                // Più file in coda: calcolo della concorrenza ideale tramite radice quadrata
                int idealConcurrent = static_cast<int>(std::sqrt(targetThreadsBudget));
                if (idealConcurrent < 1)
                    idealConcurrent = 1;

                // Applica il limite di sicurezza basato sulla cache/disco
                idealConcurrent = (std::min)(idealConcurrent, maxAllowedConcurrent);

                // Imposta la concorrenza reale limitata dai task effettivamente pendenti
                strategy.maxConcurrentTasks =
                    (std::min)(idealConcurrent, static_cast<int>(pendingTasks));

                // Dividiamo i thread allocati per ogni file in chunk da ~6 thread ciascuno
                int threadsPerFile = targetThreadsBudget / strategy.maxConcurrentTasks;
                strategy.chunksPerTask = std::clamp(threadsPerFile / 6, 1, 4);
            }
        } else {
            // L'utente ha forzato un valore fisso per i chunk
            strategy.chunksPerTask = userChunks;

            int idealConcurrent = targetThreadsBudget / (strategy.chunksPerTask * 6);
            idealConcurrent = (std::min)(idealConcurrent, maxAllowedConcurrent);
            if (idealConcurrent < 1)
                idealConcurrent = 1;
            strategy.maxConcurrentTasks =
                (std::min)(idealConcurrent, static_cast<int>(pendingTasks));
        }

        // 5. Calcolo finale dei thread effettivi per singolo comando FFmpeg
        int totalProcesses = strategy.maxConcurrentTasks * strategy.chunksPerTask;
        int t = targetThreadsBudget / totalProcesses;

        // Assegna almeno 1 thread, max 12 (oltre i 12 x265 scala male)
        strategy.threadsPerFFmpeg = std::clamp(t, 1, 12);

        return strategy;
    }

} // namespace Config