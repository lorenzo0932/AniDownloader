#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <map>
#include <atomic>
#include <iomanip>
#include <chrono>
#include "core/SeriesRepository.hpp"
#include "core/ExecutionEngine.hpp"
#include "config/AppConfigManager.hpp"
#include "gui/MainWindow.hpp"
#include <QApplication>
#include <QSurfaceFormat>

using namespace Core;

// Sincronizzazione per la console
std::mutex g_statusMutex;
std::map<std::string, std::string> g_statusMap;
std::vector<TaskReport> g_reports;
auto g_startTime = std::chrono::steady_clock::now();

/**
 * @brief Aggiorna la dashboard nel terminale.
 */
void refreshTerminal(bool burst) {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    
    // ANSI: Sposta cursore in 0,0 (Home)
    std::cout << "\033[H"; 
    
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - g_startTime).count();
    
    std::cout << "==========================================================\n";
    std::cout << "   AniDownloader C++ | Mod: " << (burst ? "BURST 🚀" : "SILENT ☁️") << " | T: " << elapsed << "s\n";
    std::cout << "==========================================================\n";
    
    for (auto const& [name, status] : g_statusMap) {
        std::string disp = (name.length() > 34) ? name.substr(0, 31) + "..." : name;
        std::cout << " - " << std::left << std::setw(35) << disp << " : " << status << "\033[K\n";
    }
    std::cout << "==========================================================\n" << std::flush;
}

int main(int argc, char* argv[]) {
    // --- OTTIMIZZAZIONI HARDWARE UI (Qt 6) ---
    // In Qt 6, l'HighDPI è attivo di default, non serve richiamarlo.
    
    // Configuriamo la superficie di rendering per la massima fluidità
    QSurfaceFormat format;
    format.setSamples(4);      // Anti-aliasing hardware
    format.setSwapInterval(1); // Abilita V-Sync
    QSurfaceFormat::setDefaultFormat(format);

    bool burstMode = false;
    bool guiMode = false;

    // Parsing argomenti
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--burst") burstMode = true;
        if (arg == "--gui") guiMode = true;
    }

    if (guiMode) {
        QApplication app(argc, argv);
        app.setDesktopSettingsAware(true);

        Gui::MainWindow window;
        window.show();
        return app.exec();
    }

    // --- LOGICA CLI ---
    Config::AppConfigManager configManager;
    SeriesRepository repo(configManager.get<std::string>("json_file_path", ""));
    auto seriesList = repo.loadSeriesData();
    
    if (seriesList.empty()) {
        std::cout << "Nessuna serie trovata nel database JSON.\n";
        return 0;
    }

    // Preparazione terminale
    std::cout << "\033[2J\033[H"; 
    
    ExecutionEngine engine(configManager);
    std::atomic<bool> stop(false);

    engine.run(seriesList, burstMode, stop,
        // 4. ProgressCb
        [&](const std::string& n, const std::string& m) {
            {
                std::lock_guard<std::mutex> l(g_statusMutex);
                g_statusMap[n] = m;
            } 
            refreshTerminal(burstMode);
        },
        // 5. StatusCb (Globale)
        [&](const std::string& s) {
            (void)s; // Silenzia il warning dell'unused parameter
        },
        // 6. FinishedCb
        [&](const TaskReport& r) {
            std::lock_guard<std::mutex> l(g_statusMutex);
            g_reports.push_back(r);
        },
        // 7. SkippedCb
        [&](const std::string& n, const std::string& r) {
            {
                std::lock_guard<std::mutex> l(g_statusMutex);
                g_statusMap[n] = "🚫 " + r;
            }
            refreshTerminal(burstMode);
        },
        // 8. AnalysisCb
        nullptr
    );

    // Visualizzazione finale resoconto
    std::cout << "\n\n--- RESOCONTO FINALE ---\n";
    for (const auto& r : g_reports) {
        if (!r.success) {
            std::cout << "❌ " << std::left << std::setw(35) << r.name << " | Errore: " << r.error << "\n";
        } else {
            std::cout << "✅ " << std::left << std::setw(35) << r.name 
                      << " | DL: " << std::fixed << std::setprecision(1) << r.dlTime << "s"
                      << " | Conv: " << r.convTime << "s\n";
        }
    }
    
    return 0;
}