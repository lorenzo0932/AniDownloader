#include "gui/DownloadWorker.hpp"
#include "core/ExecutionEngine.hpp"
#ifndef _WIN32
    #include <unistd.h>
#else
    #include <windows.h>
#endif

namespace Gui {

DownloadWorker::DownloadWorker(const std::vector<Core::Series>& seriesList,
                               const std::string& jsonFilePath,
                               const std::string& logFilePath,
                               const std::string& outputDir)
    : m_seriesList(seriesList), m_jsonFilePath(jsonFilePath),
      m_logFilePath(logFilePath), m_outputDir(outputDir), m_stopSignal(false) {}

void DownloadWorker::run() {
    Core::ExecutionEngine engine(m_configManager);

    engine.run(
        m_seriesList, 
        true, 
        m_stopSignal,
        // Progress
        [this](const std::string& n, const std::string& m) {
            emit progress(QString::fromStdString(n), QString::fromStdString(m));
        },
        // Status
        [this](const std::string& s) {
            emit overallStatus(QString::fromStdString(s));
        },
        // Finished
        [this](const Core::TaskReport& r) {
            if (r.success) {
                emit finished(QString::fromStdString(r.name), "✅ Fatto", r.dlTime, r.convTime);
            } else {
                emit error(QString::fromStdString(r.name), QString::fromStdString(r.error));
            }
        },
        // Skipped (Ripristinato)
        [this](const std::string& name, const std::string& reason) {
            emit taskSkipped(QString::fromStdString(name), "🚫 " + QString::fromStdString(reason));
        },
        // Analysis Done
        [this]() { emit analysisFinished(); }
    );

    emit allWorkFinished();
}

void DownloadWorker::requestStop() {
    m_stopSignal = true;
    
#ifndef _WIN32
    // Ottiene il PID del processo corrente ed elimina esclusivamente i suoi processi figli diretti (aria2c, ffmpeg, ffprobe, ecc.)
    // lasciando intatti i processi degli altri programmi di sistema.
    pid_t myPid = getpid();
    std::string killCmd = "pids=$(pgrep -P " + std::to_string(myPid) + " 2>/dev/null); for pid in $pids; do pkill -9 -P $pid 2>/dev/null; kill -9 $pid 2>/dev/null; done";
    std::system(killCmd.c_str());
#else
    // Su Windows, facciamo la stessa operazione mirata con taskkill filtrando per PPID (Parent Process ID)
    DWORD myPid = GetCurrentProcessId();
    std::string killCmd = "taskkill /F /FI \"PPID eq " + std::to_string(myPid) + "\" /T >nul 2>&1";
    std::system(killCmd.c_str());
#endif

    Core::MediaProcessor::notifyStop();
}

} // namespace Gui