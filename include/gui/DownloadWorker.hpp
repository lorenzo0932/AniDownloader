#ifndef DOWNLOADWORKER_HPP
#define DOWNLOADWORKER_HPP

#include <QObject>
#include <QString>
#include <vector>
#include <atomic>
#include "core/Series.hpp"
#include "core/MediaProcessor.hpp"
#include "core/PlanningService.hpp"
#include "config/AppConfigManager.hpp"

namespace Gui {

    /**
     * @brief Worker che gestisce il thread di download e conversione per la GUI.
     */
    class DownloadWorker : public QObject {
        Q_OBJECT

    public:
        explicit DownloadWorker(const std::vector<Core::Series>& seriesList,
                                const std::string& jsonFilePath,
                                const std::string& logFilePath,
                                const std::string& outputDir);

    public slots:
        /**
         * @brief Avvia il ciclo di elaborazione delle serie.
         */
        void run();

        /**
         * @brief Segnala l'interruzione forzata dei processi esterni (aria2/ffmpeg).
         */
        void requestStop();

    signals:
        void progress(const QString& seriesName, const QString& statusMessage);
        void error(const QString& seriesName, const QString& errorMessage);
        void finished(const QString& seriesName, const QString& epPath, double dlTime, double convTime);
        void taskSkipped(const QString& seriesName, const QString& reason);
        void analysisFinished(); 
        void overallStatus(const QString& status);
        void allWorkFinished();

    private:
        std::vector<Core::Series> m_seriesList;
        std::string m_jsonFilePath;
        std::string m_logFilePath;
        std::string m_outputDir;
        std::atomic<bool> m_stopSignal;

        Config::AppConfigManager m_configManager;
    };

}

#endif // DOWNLOADWORKER_HPP