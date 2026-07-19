#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QMainWindow>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include <QTextEdit>
#include <QSplitter>
#include <QThread>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QSettings>
#include <QTimer>
#include <QProgressBar>
#include <QMap>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <memory>
#include <vector>

#include "config/AppConfigManager.hpp"
#include "core/SeriesRepository.hpp"
#include "core/Series.hpp"
#include "gui/Widgets.hpp"
#include "gui/SlidingContainer.hpp"
#include "gui/SeriesManagerWidget.hpp"   // La nuova vista manager
#include "gui/DownloadWorker.hpp"        // Il tuo worker per il download

namespace Gui {

    class MainWindow : public QMainWindow {
        Q_OBJECT

    public:
        explicit MainWindow(QWidget *parent = nullptr);
        ~MainWindow() override;

    protected:
        void closeEvent(QCloseEvent *event) override;
        void changeEvent(QEvent *event) override;
        void dragEnterEvent(QDragEnterEvent *event) override;
        void dropEvent(QDropEvent *event) override;

    private slots:
        // Slot per il controllo dei download
        void startDownload();
        void stopDownload();
        void refreshSeries();
        void openSettings();
        void resetTableSort();

        // Slot di comunicazione con il DownloadWorker
        void updateSeriesStatus(const QString& seriesName, const QString& statusMessage);
        void handleWorkerError(const QString& seriesName, const QString& errorMessage);
        void handleSeriesFinished(const QString& seriesName, const QString& epPath, double dlTime, double convTime);
        void handleTaskSkipped(const QString& seriesName, const QString& reason);
        void updateOverallStatus(const QString& status);
        void onDownloadFinished();
        void onProgressDelayTimer();
        
        // UI Interaction
        void onSeriesSelected();
        void applyThemeOnEvent();
        void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);

        // --- NUOVI SLOT PER ARCHITETTURA SLIDER ---
        void switchView(int index);
        void onSeriesDataChanged();

    private:
        // Setup UI
        void initUi();
        void initDownloadView(QWidget *parentWidget); // Scorporata per lo slider
        void initTrayIcon();
        
        // Logica Dati e Config
        void loadConfigPaths();
        void checkSeriesFile();
        void loadSeriesDataIntoTable(int rowToSelect = -1);
        void populateTable(const std::vector<Core::Series>& seriesList, int rowToSelect = -1, bool scrollToSelected = false);
        void executeStopProcedure();
        void setUiStateForDownload(bool inProgress);
        void restoreGeometryAndState();
        void applyTheme();

        // --- COMPONENTI UI SLIDER ---
        SlidingContainer *m_slider;
        QWidget *m_downloadView;
        SeriesManagerWidget *m_managerView;
        QPushButton *m_tabDownloadBtn;
        QPushButton *m_tabManagerBtn;

        // --- CORE & CONFIG ---
        std::unique_ptr<Config::AppConfigManager> m_configManager;
        std::unique_ptr<QSettings> m_settings;
        std::unique_ptr<Core::SeriesRepository> m_seriesRepository;

        QString m_jsonFilePath;
        QString m_outputDir;
        QString m_logFilePath;
        std::vector<Core::Series> m_seriesData;

        bool m_isDarkTheme = true;
        QTimer *m_themeDebounceTimer;

        // --- COMPONENTI VISTA DOWNLOAD ---
        QPushButton *m_startButton;
        QPushButton *m_stopButton;
        QPushButton *m_refreshButton;
        QPushButton *m_resetSortButton;
        QPushButton *m_settingsButton;

        QTableWidget *m_tableWidget;
        QLabel *m_imageLabel;
        QTextEdit *m_logOutput;
        QSplitter *m_mainSplitter;
        QLabel *m_overallStatusLabel;
        QProgressBar *m_globalProgressBar;
        QMap<QString, int> m_seriesProgressMap;
        QMap<QString, int> m_seriesPhaseMode;
        bool m_globalProgressActive = false;
        QTimer *m_progressDelayTimer;

        // --- SYSTEM TRAY ---
        QSystemTrayIcon *m_trayIcon;
        QMenu *m_trayMenu;

        // --- THREADING ---
        QThread *m_downloadThread = nullptr;
        DownloadWorker *m_downloadWorker = nullptr;
    };

} // namespace Gui

#endif // MAINWINDOW_HPP