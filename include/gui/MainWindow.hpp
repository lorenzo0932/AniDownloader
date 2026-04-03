#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QMainWindow>
#include <QString>
#include <QTableWidget>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QSplitter>
#include <QSettings>
#include <QTimer>
#include <QThread>
#include <QSystemTrayIcon>
#include <QMenu>
#include <memory>
#include <vector>

#include "core/Series.hpp"
#include "core/SeriesRepository.hpp"
#include "config/AppConfigManager.hpp"
#include "gui/DownloadWorker.hpp"

namespace Gui {

    class MainWindow : public QMainWindow {
        Q_OBJECT

    public:
        explicit MainWindow(QWidget *parent = nullptr);
        ~MainWindow();

    protected:
        void changeEvent(QEvent *event) override;
        void closeEvent(QCloseEvent *event) override;

    private slots:
        void startDownload();
        void stopDownload();
        void refreshSeries();
        void openSeriesManager();
        void openSettings();
        void resetTableSort();
        void onSeriesSelected();
        void updateSeriesStatus(const QString& seriesName, const QString& statusMessage);
        void handleWorkerError(const QString& seriesName, const QString& errorMessage);
        void handleSeriesFinished(const QString& seriesName, const QString& epPath, double dlTime, double convTime);
        void handleTaskSkipped(const QString& seriesName, const QString& reason);
        void updateOverallStatus(const QString& status);
        void onDownloadFinished();
        void applyTheme();
        void applyThemeOnEvent();
        void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);

    private:
        void initTrayIcon();
        void initUi();
        void loadConfigPaths();
        void checkSeriesFile();
        void loadSeriesDataIntoTable(int rowToSelect = 0);
        void populateTable(const std::vector<Core::Series>& seriesList, int rowToSelect = 0, bool scrollToSelected = true);
        void restoreGeometryAndState();
        void setUiStateForDownload(bool inProgress);
        void executeStopProcedure();

        // UI Components
        QPushButton *m_startButton;
        QPushButton *m_stopButton;
        QPushButton *m_refreshButton;
        QPushButton *m_manageSeriesButton;
        QPushButton *m_resetSortButton;
        QPushButton *m_settingsButton;
        QTableWidget *m_tableWidget;
        QLabel *m_imageLabel;
        QTextEdit *m_logOutput;
        QSplitter *m_mainSplitter;
        QLabel *m_overallStatusLabel;
        QSystemTrayIcon *m_trayIcon;
        QMenu *m_trayMenu;

        // Backend
        std::unique_ptr<Config::AppConfigManager> m_configManager;
        std::unique_ptr<Core::SeriesRepository> m_seriesRepository;
        std::vector<Core::Series> m_seriesData;
        std::unique_ptr<QSettings> m_settings;
        
        QTimer *m_themeDebounceTimer;
        bool m_isDarkTheme = false;

        QString m_jsonFilePath;
        QString m_outputDir;
        QString m_logFilePath;

        // Multi-threading
        QThread *m_downloadThread = nullptr;
        DownloadWorker *m_downloadWorker = nullptr;
    };

}

#endif // MAINWINDOW_HPP
