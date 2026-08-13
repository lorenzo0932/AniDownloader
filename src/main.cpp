#include <iostream>
#include <vector>
#include <string>
#include <mutex>
#include <map>
#include <atomic>
#include <iomanip>
#include <chrono>
#include <csignal>
#include <thread>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#include "core/SeriesRepository.hpp"
#include "core/ExecutionEngine.hpp"
#include "core/MediaProcessor.hpp"
#include "core/Logger.hpp"
#include "config/AppConfigManager.hpp"
#include "config/PathHelper.hpp"
#include "web/WebServer.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace Core;

// Sincronizzazione per la console
std::mutex g_statusMutex;
std::map<std::string, std::string> g_statusMap;
std::vector<TaskReport> g_reports;
std::string g_overallStatus;
auto g_startTime = std::chrono::steady_clock::now();
std::atomic<bool> *g_stopPtr = nullptr;
static std::atomic<bool> g_cleanupRequested{false};
static std::atomic<bool> *g_webRunningPtr = nullptr;

// Esegue la kill-tree dei processi figli (aria2, chromedriver). Solo in contesto thread normale.
static void killProcessTree() {
#ifndef _WIN32
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "pids=$(pgrep -P %d 2>/dev/null); "
             "for pid in $pids; do pkill -9 -P $pid 2>/dev/null; kill -9 $pid 2>/dev/null; done; "
             "pkill -x chromedriver 2>/dev/null",
             static_cast<int>(getpid()));
    std::system(cmd);
#else
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "taskkill /F /FI \"PPID eq %lu\" /T >nul 2>&1",
             static_cast<unsigned long>(GetCurrentProcessId()));
    std::system(cmd);
#endif
}

// Handler di segnale async-signal-safe: imposta SOLO flag atomici.
// La pulizia (kill-tree, notifyStop) avviene nel watchdog thread, in contesto normale.
static void cliSignalHandler(int) {
    if (g_stopPtr) g_stopPtr->store(true, std::memory_order_relaxed);
    if (g_webRunningPtr) g_webRunningPtr->store(false, std::memory_order_relaxed);
    g_cleanupRequested.store(true, std::memory_order_relaxed);
}

// Watchdog: quando arriva un segnale esegue la pulizia in contesto thread normale,
// evitando std::system e lock di mutex dentro il signal handler (UB/deadlock).
static void startCleanupWatchdog() {
    std::thread([]() {
        while (!g_cleanupRequested.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        killProcessTree();
        MediaProcessor::notifyStop();
    }).detach();
}

/**
 * @brief Aggiorna la dashboard nel terminale.
 */
static const char* spinnerFrames = "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏";

void refreshTerminal(bool burst) {
    static int spinIdx = 0;
    std::lock_guard<std::mutex> lock(g_statusMutex);

    std::cout << "\033[H";

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - g_startTime).count();

    std::cout << "==========================================================\n";
    bool hasAnalisi = false;
    for (auto const& [name, status] : g_statusMap) {
        if (status.find("Analisi:") != std::string::npos) { hasAnalisi = true; break; }
    }
    char spin = spinnerFrames[spinIdx % 10];
    if (!hasAnalisi) spin = ' ';
    spinIdx++;

    std::cout << "   " << spin << " AniDownloader C++ | Mod: " << (burst ? "BURST 🚀" : "SILENT ☁️") << " | T: " << elapsed << "s\n";
    std::cout << "==========================================================\n";

    if (g_statusMap.empty()) {
        std::cout << g_overallStatus << "\033[K\n";
    } else {
        for (auto const& [name, status] : g_statusMap) {
            std::string disp = (name.length() > 34) ? name.substr(0, 31) + "..." : name;
            std::cout << " - " << std::left << std::setw(35) << disp << " : " << status << "\033[K\n";
        }
    }
    std::cout << "==========================================================\n" << std::flush;
}

int main(int argc, char* argv[]) {
    bool burstMode = false;
    bool webMode = false;
    bool silentMode = false;
    int webPort = 8989;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--burst") burstMode = true;
        if (arg == "--web") webMode = true;
        if (arg == "--silent") silentMode = true;
        if (arg == "--port" && i + 1 < argc) {
            try {
                webPort = std::stoi(argv[i + 1]);
            } catch (const std::exception&) {
                std::cerr << "Porta non valida: " << argv[i + 1] << "\n";
                return 1;
            }
            ++i;
        }
    }

    if (webMode) {
        Config::AppConfigManager configManager;
        Core::Logger::init(configManager.get<std::string>(
            "log_file_path", Config::PathHelper::getLogFilePath().string()));

        auto server = std::make_shared<Web::WebServer>(configManager, webPort);

        std::atomic<bool> running{true};
        std::thread serverThread([server, &running]() {
            if (!server->start()) {
                Core::Logger::error("Failed to start web server");
                running = false;
            }
        });

        g_webRunningPtr = &running;
#ifndef _WIN32
        struct sigaction sa{};
        sa.sa_handler = cliSignalHandler;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
#else
        SetConsoleCtrlHandler([](DWORD) -> BOOL {
            cliSignalHandler(0);
            return TRUE;
        }, TRUE);
#endif
        startCleanupWatchdog();

        int finalPort = server->activePort();
        if (!silentMode) {
            auto lanIp = Web::WebServer::getLanIp();
            std::cout << "Web UI: http://localhost:" << finalPort << "\n";
            if (lanIp != "0.0.0.0" && lanIp != "127.0.0.1")
                std::cout << "Web UI (LAN): http://" << lanIp << ":" << finalPort << "\n";
        }

        while (running.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        server->stop();
        if (serverThread.joinable()) serverThread.join();
        return 0;
    }

    // --- LOGICA CLI ---
#ifdef _WIN32
    // Abilita Virtual Terminal Processing per supportare ANSI escape codes su Windows 10+
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
#endif

    Config::AppConfigManager configManager;
    Core::Logger::init(configManager.get<std::string>("log_file_path", Config::PathHelper::getLogFilePath().string()));
    SeriesRepository repo(configManager.get<std::string>("json_file_path", ""));
    auto seriesList = repo.loadSeriesData();
    
    if (seriesList.empty()) {
        std::cout << "Nessuna serie trovata nel database JSON.\n";
        return 0;
    }

    if (burstMode) std::cout << "\033[2J\033[H";

    ExecutionEngine engine(configManager);
    std::atomic<bool> stop(false);
    g_stopPtr = &stop;
    #ifndef _WIN32
        struct sigaction sa{};
        sa.sa_handler = cliSignalHandler;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
    #else
        SetConsoleCtrlHandler([](DWORD) -> BOOL {
            cliSignalHandler(0);
            return TRUE;
        }, TRUE);
    #endif

    startCleanupWatchdog();

    engine.run(seriesList, burstMode, stop,
        [&](const std::string& n, int ep, const std::string& m) {
            {
                std::lock_guard<std::mutex> l(g_statusMutex);
                std::string key = (ep > 0) ? n + " (Ep " + std::to_string(ep) + ")" : n;
                if (ep > 0) g_statusMap.erase(n);
                g_statusMap[key] = m;
            }
            if (burstMode) refreshTerminal(burstMode);
        },
        [&](const std::string& s) {
            {
                std::lock_guard<std::mutex> l(g_statusMutex);
                g_overallStatus = s;
            }
            if (burstMode) refreshTerminal(burstMode);
        },
        [&](const TaskReport& r) {
            {
                std::lock_guard<std::mutex> l(g_statusMutex);
                g_reports.push_back(r);
            }
        },
        [&](const std::string&, const std::string&) {
        },
        nullptr
    );

    // Salvataggio unico post-esecuzione: lastDownloadedEpisode per serie
    {
        std::map<std::string, int> maxEpisodes;
        for (const auto& r : g_reports) {
            if (r.success && r.episodeNumber > 0) {
                maxEpisodes[r.name] = (std::max)(maxEpisodes[r.name], r.episodeNumber);
            }
        }

        if (!maxEpisodes.empty()) {
            auto now = std::chrono::system_clock::now();
            auto tt = std::chrono::system_clock::to_time_t(now);
            auto tm = *std::gmtime(&tt);
            char ts[24] = {};
            std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm);

            repo.applyDownloadedEpisodes(maxEpisodes, ts);
        }
    }

    // Visualizzazione finale resoconto
    std::cout << "\n\n--- RESOCONTO FINALE ---\n";
    for (const auto& r : g_reports) {
        std::string label = (r.episodeNumber > 0) ? r.name + " (Ep " + std::to_string(r.episodeNumber) + ")" : r.name;
        if (!r.success) {
            std::cout << "❌ " << std::left << std::setw(35) << label << " | Errore: " << r.error << "\n";
        } else {
            std::cout << "✅ " << std::left << std::setw(35) << label
                      << " | DL: " << std::fixed << std::setprecision(1) << r.dlTime << "s"
                      << " | Conv: " << r.convTime << "s\n";
        }
    }

    if (burstMode) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    return 0;
}