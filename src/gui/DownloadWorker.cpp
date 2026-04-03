#include "gui/DownloadWorker.hpp"
#include "core/ExecutionEngine.hpp"

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
    std::system("killall -9 aria2c ffmpeg ffprobe 2>/dev/null");
    Core::MediaProcessor::notifyStop();
}

} // namespace Gui