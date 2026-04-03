#include "gui/MainWindow.hpp"
#include "gui/Styles.hpp"
#include "gui/Widgets.hpp"
#include "gui/SettingsDialog.hpp"
#include "gui/SeriesManagerDialog.hpp"
#include "config/PathHelper.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QApplication>
#include <QScreen>
#include <QStyle>
#include <QMessageBox>
#include <QFileDialog>
#include <QScrollBar>
#include <QPalette>
#include <QEvent>
#include <QCloseEvent>
#include <QRegularExpression>

namespace Gui {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("AniDownloader GUI");
    
    m_configManager = std::make_unique<Config::AppConfigManager>();
    m_settings = std::make_unique<QSettings>(
        QString::fromStdString(Config::PathHelper::getConfigDir().string() + "/AniDownloader.conf"),
        QSettings::IniFormat
    );

    m_themeDebounceTimer = new QTimer(this);
    m_themeDebounceTimer->setSingleShot(true);
    connect(m_themeDebounceTimer, &QTimer::timeout, this, &MainWindow::applyThemeOnEvent);

    // Inizializza prima l'UI in modo che m_tableWidget sia disponibile per applyTheme()
    initUi();
    initTrayIcon();

    applyTheme();

    // Center window
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect screenGeometry = screen->geometry();
    int windowWidth = 1000;
    int windowHeight = 700;
    int x = (screenGeometry.width() - windowWidth) / 2;
    int y = (screenGeometry.height() - windowHeight) / 2;
    setGeometry(x, y, windowWidth, windowHeight);
    setMinimumSize(850, 600);

    // setWindowIcon(QIcon("resources/logo.png")); // To be added to resources

    loadConfigPaths();
    m_seriesRepository = std::make_unique<Core::SeriesRepository>(m_jsonFilePath.toStdString());
    checkSeriesFile();

    loadSeriesDataIntoTable();
    restoreGeometryAndState();
}

MainWindow::~MainWindow() {}

void MainWindow::initUi() {
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    QWidget *topContainer = new QWidget(this);
    QVBoxLayout *topLayout = new QVBoxLayout(topContainer);
    topLayout->setContentsMargins(0, 0, 0, 0);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(12);

    m_startButton = new QPushButton("Avvia Download", this);
    m_startButton->setObjectName("primaryButton");
    m_startButton->setFixedSize(160, 45);
    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::startDownload);

    m_stopButton = new QPushButton("Ferma Download", this);
    m_stopButton->setObjectName("dangerButton");
    m_stopButton->setFixedSize(160, 45);
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::stopDownload);

    m_refreshButton = new QPushButton("Aggiorna Serie", this);
    m_refreshButton->setMinimumSize(140, 45);
    m_refreshButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::refreshSeries);

    m_manageSeriesButton = new QPushButton("Gestisci Serie", this);
    m_manageSeriesButton->setMinimumSize(140, 45);
    m_manageSeriesButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    connect(m_manageSeriesButton, &QPushButton::clicked, this, &MainWindow::openSeriesManager);

    m_resetSortButton = new QPushButton("Reset Ordine", this);
    m_resetSortButton->setMinimumSize(140, 45);
    m_resetSortButton->setIcon(style()->standardIcon(QStyle::SP_DialogResetButton));
    connect(m_resetSortButton, &QPushButton::clicked, this, &MainWindow::resetTableSort);

    m_settingsButton = new QPushButton(this);
    m_settingsButton->setFixedSize(45, 45);
    m_settingsButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    connect(m_settingsButton, &QPushButton::clicked, this, &MainWindow::openSettings);

    buttonLayout->addWidget(m_startButton);
    buttonLayout->addWidget(m_stopButton);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(m_refreshButton);
    buttonLayout->addWidget(m_manageSeriesButton);
    buttonLayout->addWidget(m_resetSortButton);
    buttonLayout->addSpacing(10);
    buttonLayout->addWidget(m_settingsButton);

    topLayout->addLayout(buttonLayout);
    topLayout->addSpacing(15);

    // Table Configuration
    m_tableWidget = new QTableWidget(this);
    m_tableWidget->setColumnCount(2);
    m_tableWidget->setHorizontalHeaderLabels({"Nome Serie", "Stato"});
    m_tableWidget->verticalHeader()->setVisible(false);
    m_tableWidget->setAlternatingRowColors(true);
    m_tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableWidget->setSortingEnabled(true);

    QHeaderView *header = m_tableWidget->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    header->setSectionResizeMode(1, QHeaderView::Interactive);
    m_tableWidget->setColumnWidth(1, 230);
    header->setMinimumSectionSize(200);

    connect(m_tableWidget, &QTableWidget::itemSelectionChanged, this, &MainWindow::onSeriesSelected);
    m_tableWidget->setItemDelegateForColumn(1, new ProgressBarDelegate(this));

    m_imageLabel = new QLabel(this);
    m_imageLabel->setFixedSize(220, 320);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet("border: 2px solid gray; border-radius: 6px;");
    m_imageLabel->setText("Nessuna Immagine");

    QHBoxLayout *seriesDisplayLayout = new QHBoxLayout();
    seriesDisplayLayout->addWidget(m_tableWidget);
    seriesDisplayLayout->addWidget(m_imageLabel);
    topLayout->addLayout(seriesDisplayLayout);

    m_logOutput = new QTextEdit(this);
    m_logOutput->setReadOnly(true);

    m_mainSplitter = new QSplitter(Qt::Vertical, this);
    m_mainSplitter->addWidget(topContainer);
    m_mainSplitter->addWidget(m_logOutput);
    m_mainSplitter->setStretchFactor(0, 3);
    m_mainSplitter->setStretchFactor(1, 1);
    mainLayout->addWidget(m_mainSplitter);

    m_overallStatusLabel = new QLabel("Pronto.", this);
    m_overallStatusLabel->setStyleSheet("font-size: 10pt; font-weight: bold;");
    mainLayout->addWidget(m_overallStatusLabel);
}

void MainWindow::loadConfigPaths() {
    m_jsonFilePath = QString::fromStdString(Config::PathHelper::getSeriesJsonPath().string());
    m_outputDir = QString::fromStdString(Config::PathHelper::getVideosDir().string());
    m_logFilePath = QString::fromStdString(Config::PathHelper::getLogFilePath().string());
}

void MainWindow::checkSeriesFile() {
    if (!std::filesystem::exists(m_jsonFilePath.toStdString())) {
        m_seriesRepository->saveSeriesData({});
    }
}

void MainWindow::loadSeriesDataIntoTable(int rowToSelect) {
    try {
        m_seriesData = m_seriesRepository->loadSeriesData();
    } catch (...) {
        m_seriesData.clear();
    }
    populateTable(m_seriesData, rowToSelect);
}

void MainWindow::populateTable(const std::vector<Core::Series>& seriesList, int rowToSelect, bool scrollToSelected) {
    m_tableWidget->setSortingEnabled(false);
    m_tableWidget->setRowCount(0);
    m_tableWidget->setRowCount(static_cast<int>(seriesList.size()));

    for (size_t row = 0; row < seriesList.size(); ++row) {
        QTableWidgetItem *nameItem = new QTableWidgetItem(QString::fromStdString(seriesList[row].name));
        ProgressBarTableWidgetItem *statusItem = new ProgressBarTableWidgetItem("In attesa", 3);
        m_tableWidget->setItem(row, 0, nameItem);
        m_tableWidget->setItem(row, 1, statusItem);
    }

    if (!seriesList.empty() && rowToSelect != -1) {
        rowToSelect = std::min(std::max(0, rowToSelect), static_cast<int>(seriesList.size()) - 1);
        m_tableWidget->setCurrentCell(rowToSelect, 0);
        if (scrollToSelected) {
            m_tableWidget->scrollToItem(m_tableWidget->item(rowToSelect, 0));
        }
        onSeriesSelected();
    }
    m_tableWidget->setSortingEnabled(true);
}

void MainWindow::onSeriesSelected() {
    auto selectedItems = m_tableWidget->selectedItems();
    if (selectedItems.isEmpty()) return;

    int row = m_tableWidget->currentRow();
    QTableWidgetItem *item = m_tableWidget->item(row, 0);
    if (!item) return;

    QString seriesName = item->text();
    
    // Find the series in m_seriesData
    auto it = std::find_if(m_seriesData.begin(), m_seriesData.end(), [&](const Core::Series& s) {
        return QString::fromStdString(s.name) == seriesName;
    });

    if (it != m_seriesData.end()) {
        std::filesystem::path seriesPath(it->path);
        std::filesystem::path folderPath = seriesPath.parent_path();
        std::filesystem::path imagePath = folderPath / "folder.jpg";

        if (std::filesystem::exists(imagePath)) {
            QPixmap pixmap(QString::fromStdString(imagePath.string()));
            if (!pixmap.isNull()) {
                m_imageLabel->setPixmap(pixmap.scaled(m_imageLabel->size(),
                                                      Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation));
            } else {
                m_imageLabel->setText("Locandina non valida");
            }
        } else {
            m_imageLabel->setText("Locandina non trovata");
        }
    } else {
        m_imageLabel->clear();
        m_imageLabel->setText("Serie non trovata");
    }
}

void MainWindow::startDownload() {
    if (m_seriesData.empty()) return;
    setUiStateForDownload(true);
    m_logOutput->clear();

    m_downloadThread = new QThread(this);
    
    // Nuovo costruttore semplificato: i parametri tecnici li gestisce il Worker 
    // tramite il suo AppConfigManager interno.
    m_downloadWorker = new DownloadWorker(
        m_seriesData, 
        m_jsonFilePath.toStdString(),
        m_logFilePath.toStdString(),
        m_outputDir.toStdString()
    );

    m_downloadWorker->moveToThread(m_downloadThread);

    connect(m_downloadThread, &QThread::started, m_downloadWorker, &DownloadWorker::run);
    connect(m_downloadWorker, &DownloadWorker::progress, this, &MainWindow::updateSeriesStatus);
    connect(m_downloadWorker, &DownloadWorker::error, this, &MainWindow::handleWorkerError);
    connect(m_downloadWorker, &DownloadWorker::finished, this, &MainWindow::handleSeriesFinished);
    connect(m_downloadWorker, &DownloadWorker::taskSkipped, this, &MainWindow::handleTaskSkipped);
    connect(m_downloadWorker, &DownloadWorker::overallStatus, this, &MainWindow::updateOverallStatus);
    connect(m_downloadWorker, &DownloadWorker::allWorkFinished, this, &MainWindow::onDownloadFinished);

    // Ordina la lista una volta sola alla fine dell'analisi
    connect(m_downloadWorker, &DownloadWorker::analysisFinished, this, [this]() {
        m_tableWidget->sortItems(1, Qt::AscendingOrder);
    });
    
    connect(m_downloadThread, &QThread::finished, m_downloadWorker, &QObject::deleteLater);
    connect(m_downloadThread, &QThread::finished, m_downloadThread, &QObject::deleteLater);

    m_downloadThread->start();
}

void MainWindow::stopDownload() {
    if (m_settings->value("show_stop_warning", true).toBool()) {
        StopConfirmationDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted) {
            if (dialog.dontShowAgain()) {
                m_settings->setValue("show_stop_warning", false);
            }
            executeStopProcedure();
        }
    } else {
        executeStopProcedure();
    }
}

void MainWindow::executeStopProcedure() {
    if (m_downloadWorker) {
        updateOverallStatus("Interruzione in corso...");
        m_stopButton->setEnabled(false);
        m_downloadWorker->requestStop();
    }
}

void MainWindow::refreshSeries() {
    loadSeriesDataIntoTable();
}

void MainWindow::openSeriesManager() {
    SeriesManagerDialog dialog(m_seriesRepository.get(), this);
    if (dialog.exec() == QDialog::Accepted) {
        loadSeriesDataIntoTable();
    }
}

void MainWindow::openSettings() {
    SettingsDialog dialog(m_configManager.get(), m_settings.get(), this);
    if (dialog.exec() == QDialog::Accepted && dialog.pathsChanged()) {
        loadConfigPaths();
        m_seriesRepository = std::make_unique<Core::SeriesRepository>(m_jsonFilePath.toStdString());
        loadSeriesDataIntoTable();
    }
}

void MainWindow::resetTableSort() {
    m_tableWidget->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    populateTable(m_seriesData);
}

void MainWindow::updateSeriesStatus(const QString& seriesName, const QString& statusMessage) {
    QString statusLower = statusMessage.toLower();
    int newPriority = 1;
    QString currentPhase = "";

    // Filtriamo i messaggi per identificare le fasi attive (che richiedono la barra)
    if (statusLower.contains("dl") || statusLower.contains("download")) { 
        currentPhase = "download"; 
        newPriority = 0; 
    }
    else if (statusLower.contains("conv") || statusLower.contains("conversione")) { 
        currentPhase = "conversion"; 
        newPriority = 0; 
    }
    else if (statusLower.contains("fatto") || statusLower.contains("completato") || statusLower.contains("✅")) { 
        newPriority = 2; 
    }
    else if (statusLower.contains("saltato") || statusLower.contains("🚫")) { 
        newPriority = 3; 
    }

    for (int row = 0; row < m_tableWidget->rowCount(); ++row) {
        if (m_tableWidget->item(row, 0)->text() == seriesName) {
            ProgressBarTableWidgetItem *item = dynamic_cast<ProgressBarTableWidgetItem*>(m_tableWidget->item(row, 1));
            if (item) {
                int progress = 0;
                bool isActive = !currentPhase.isEmpty();
                
                // Extract percentage if present
                QRegularExpression re("(\\d+)%");
                QRegularExpressionMatch match = re.match(statusMessage);
                if (match.hasMatch()) {
                    progress = match.captured(1).toInt();
                }

                item->setText(isActive ? statusMessage.split(" - ").at(0) : statusMessage);
                item->setData(Qt::UserRole + 1, progress);
                item->setData(Qt::UserRole + 2, isActive);
                item->setData(Qt::UserRole + 3, currentPhase);
                item->setPriority(newPriority);
            }
            break;
        }
    }
}

void MainWindow::handleWorkerError(const QString& seriesName, const QString& errorMessage) {
    if (seriesName == "GLOBAL" || seriesName == "DEPENDENCIES" || seriesName == "CONFIG") {
        QMessageBox::critical(this, "Errore Critico", errorMessage);
        executeStopProcedure();
    } else {
        updateSeriesStatus(seriesName, "❌ Errore");
    }
    m_logOutput->append("ERRORE [" + seriesName + "]: " + errorMessage);
}

void MainWindow::handleSeriesFinished(const QString& seriesName, const QString& epPath, double dlTime, double convTime) {
    m_logOutput->append(QString("✅ %1 | DL: %2s | Conv: %3s").arg(epPath).arg(dlTime).arg(convTime));
    updateSeriesStatus(seriesName, "✅ Fatto");

    // Notifica Desktop Native via libnotify (Linux)
#ifdef __linux__
    std::string cmd = "notify-send 'AniDownloader' 'Scaricato: " + seriesName.toStdString() + "' -i dialog-information";
    std::system(cmd.c_str());
#endif

    if (m_trayIcon) {
        m_trayIcon->showMessage("Download Completato", 
                                QString("Episodio di %1 scaricato con successo.").arg(seriesName),
                                QSystemTrayIcon::Information, 3000);
    }
}

void MainWindow::handleTaskSkipped(const QString& seriesName, const QString& reason) {
    m_logOutput->append("🚫 SKIPPED [" + seriesName + "]: " + reason);
    updateSeriesStatus(seriesName, "🚫 Saltato");
}

void MainWindow::updateOverallStatus(const QString& status) {
    m_overallStatusLabel->setText(status);
    m_logOutput->append(status);
}

void MainWindow::onDownloadFinished() {
    setUiStateForDownload(false);
    if (m_overallStatusLabel->text().contains("Interruzione")) {
        updateOverallStatus("Processo interrotto.");
    } else {
        updateOverallStatus("Processo completato.");
    }
    
    if (m_downloadThread) {
        m_downloadThread->quit();
        m_downloadThread->wait();
    }
    m_downloadThread = nullptr;
    m_downloadWorker = nullptr;
}

void MainWindow::setUiStateForDownload(bool inProgress) {
    m_tableWidget->setSortingEnabled(!inProgress);
    m_startButton->setEnabled(!inProgress);
    m_stopButton->setEnabled(inProgress);
    m_refreshButton->setEnabled(!inProgress);
    m_manageSeriesButton->setEnabled(!inProgress);
    m_resetSortButton->setEnabled(!inProgress);
    m_settingsButton->setEnabled(!inProgress);
}

void MainWindow::restoreGeometryAndState() {
    if (m_settings->contains("geometry")) restoreGeometry(m_settings->value("geometry").toByteArray());
    if (m_settings->contains("splitter_sizes")) m_mainSplitter->restoreState(m_settings->value("splitter_sizes").toByteArray());
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_downloadThread) {
        bool showWarning = m_settings->value("show_close_warning", true).toBool();
        
        CloseConfirmationDialog dialog(!showWarning, this);
        if (dialog.exec() != QDialog::Accepted) {
            event->ignore();
            return;
        }
        
        if (showWarning && dialog.dontShowAgain()) {
            m_settings->setValue("show_close_warning", false);
        }

        // Se confermato o completato l'avviso automatico, eseguiamo la procedura di stop
        executeStopProcedure();
        m_downloadThread->quit();
        m_downloadThread->wait();
    }

    m_settings->setValue("geometry", saveGeometry());
    m_settings->setValue("splitter_sizes", m_mainSplitter->saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent *event) {
    if (event->type() == QEvent::PaletteChange) {
        bool isDark = qApp->palette().color(QPalette::Window).lightness() < 128;
        if (isDark != m_isDarkTheme) {
            m_themeDebounceTimer->start(500);
        }
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::applyThemeOnEvent() {
    int vScroll = m_tableWidget->verticalScrollBar()->value();
    int currentRow = m_tableWidget->currentRow();
    QString preservedName;
    if (currentRow != -1) {
        preservedName = m_tableWidget->item(currentRow, 0)->text();
    }

    applyTheme();
    loadSeriesDataIntoTable();

    if (!preservedName.isEmpty()) {
        for (int i = 0; i < m_tableWidget->rowCount(); ++i) {
            if (m_tableWidget->item(i, 0)->text() == preservedName) {
                m_tableWidget->setCurrentCell(i, 0);
                break;
            }
        }
    }
    m_tableWidget->verticalScrollBar()->setValue(vScroll);
}

void MainWindow::initTrayIcon() {
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon)); // Fallback
    
    // Prova a caricare il logo reale
    QIcon logoIcon("resources/logo.png");
    if (!logoIcon.isNull()) {
        m_trayIcon->setIcon(logoIcon);
        setWindowIcon(logoIcon);
    }

    m_trayMenu = new QMenu(this);
    QAction *restoreAction = m_trayMenu->addAction("Ripristina");
    connect(restoreAction, &QAction::triggered, this, &MainWindow::showNormal);
    
    m_trayMenu->addSeparator();
    
    QAction *quitAction = m_trayMenu->addAction("Esci");
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->show();

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayIconActivated);
}

void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) {
        if (isVisible()) hide();
        else {
            showNormal();
            activateWindow();
        }
    }
}

void MainWindow::applyTheme() {
    bool newIsDark = qApp->palette().color(QPalette::Window).lightness() < 128;
    
    qApp->setStyleSheet(newIsDark ? DARK_THEME_QSS : LIGHT_THEME_QSS);
    
    m_isDarkTheme = newIsDark;
    if (m_tableWidget) {
        m_tableWidget->setFocusPolicy(Qt::NoFocus);
    }
}

} // namespace Gui