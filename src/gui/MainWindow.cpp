#include "gui/MainWindow.hpp"
#include "gui/Styles.hpp"
#include "gui/Widgets.hpp"
#include "gui/SettingsDialog.hpp"
#include "gui/SeriesManagerWidget.hpp"
#include "gui/ImageCache.hpp"
#include "config/PathHelper.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QApplication>
#include <QScreen>
#include <QStyle>
#include <QMessageBox>
#include <QScrollBar>
#include <QPalette>
#include <QEvent>
#include <QCloseEvent>
#include <QRegularExpression>
#include <QButtonGroup>
#include <QScroller>

namespace Gui {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    setWindowTitle("AniDownloader GUI");

    setAttribute(Qt::WA_TranslucentBackground, false);
    setAutoFillBackground(true);

    m_configManager = std::make_unique<Config::AppConfigManager>();
    m_settings = std::make_unique<QSettings>(
        QString::fromStdString(Config::PathHelper::getConfigDir().string() + "/AniDownloader.conf"),
        QSettings::IniFormat
    );

    m_themeDebounceTimer = new QTimer(this);
    m_themeDebounceTimer->setSingleShot(true);
    connect(m_themeDebounceTimer, &QTimer::timeout, this, &MainWindow::applyThemeOnEvent);

    loadConfigPaths();
    m_seriesRepository = std::make_unique<Core::SeriesRepository>(m_jsonFilePath.toStdString());
    checkSeriesFile();

    initUi();
    initTrayIcon();
    applyTheme();

    QScreen *screen = QGuiApplication::primaryScreen();
    QRect screenGeometry = screen->geometry();
    setGeometry((screenGeometry.width() - 1100) / 2, (screenGeometry.height() - 750) / 2, 1100, 750);
    setMinimumSize(850, 600);

    loadSeriesDataIntoTable();
    restoreGeometryAndState();
}

MainWindow::~MainWindow() {}

void MainWindow::initUi() {
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- NAVBAR ---
    QWidget *topNavBar = new QWidget(this);
    topNavBar->setObjectName("topNavBar");
    topNavBar->setFixedHeight(60);
    QHBoxLayout *navLayout = new QHBoxLayout(topNavBar);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(0);

    m_tabDownloadBtn = new QPushButton("DOWNLOAD", this);
    m_tabDownloadBtn->setObjectName("navTabButton");
    m_tabDownloadBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_tabDownloadBtn->setCheckable(true);
    m_tabDownloadBtn->setChecked(true);
    m_tabDownloadBtn->setCursor(Qt::PointingHandCursor);

    m_tabManagerBtn = new QPushButton("GESTIONE SERIE", this);
    m_tabManagerBtn->setObjectName("navTabButton");
    m_tabManagerBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_tabManagerBtn->setCheckable(true);
    m_tabManagerBtn->setCursor(Qt::PointingHandCursor);

    QButtonGroup *navGroup = new QButtonGroup(this);
    navGroup->addButton(m_tabDownloadBtn, 0);
    navGroup->addButton(m_tabManagerBtn, 1);
    connect(navGroup, &QButtonGroup::idClicked, this, &MainWindow::switchView);

    navLayout->addWidget(m_tabDownloadBtn);
    navLayout->addWidget(m_tabManagerBtn);
    mainLayout->addWidget(topNavBar);

    m_downloadView = new QWidget(this);
    m_downloadView->setObjectName("downloadView");
    initDownloadView(m_downloadView);

    m_managerView = new SeriesManagerWidget(m_seriesRepository.get(), this);
    m_managerView->setObjectName("managerView");
    connect(m_managerView, &SeriesManagerWidget::dataChanged, this, &MainWindow::onSeriesDataChanged);

    m_slider = new SlidingContainer(m_downloadView, m_managerView, this);
    mainLayout->addWidget(m_slider);

    connect(m_slider, &SlidingContainer::animationFinished, this, [this]() {
        m_tabDownloadBtn->setEnabled(true);
        m_tabManagerBtn->setEnabled(true);
    });
}

void MainWindow::initDownloadView(QWidget *p) {
    QVBoxLayout *l = new QVBoxLayout(p);
    l->setContentsMargins(15, 15, 15, 15);
    QWidget *tc = new QWidget(p);
    QVBoxLayout *tl = new QVBoxLayout(tc);
    tl->setContentsMargins(0, 0, 0, 0);
    QHBoxLayout *bl = new QHBoxLayout();
    bl->setSpacing(12);

    m_startButton = new QPushButton("Avvia Download", p);
    m_startButton->setObjectName("primaryButton");
    m_startButton->setFixedSize(160, 45);
    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::startDownload);

    m_stopButton = new QPushButton("Ferma Download", p);
    m_stopButton->setObjectName("dangerButton");
    m_stopButton->setFixedSize(160, 45);
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::stopDownload);

    m_refreshButton = new QPushButton("Aggiorna Serie", p);
    m_refreshButton->setMinimumSize(140, 45);
    m_refreshButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::refreshSeries);

    m_resetSortButton = new QPushButton("Reset Ordine", p);
    m_resetSortButton->setMinimumSize(140, 45);
    m_resetSortButton->setIcon(style()->standardIcon(QStyle::SP_DialogResetButton));
    connect(m_resetSortButton, &QPushButton::clicked, this, &MainWindow::resetTableSort);

    m_settingsButton = new QPushButton(p);
    m_settingsButton->setFixedSize(45, 45);
    m_settingsButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    connect(m_settingsButton, &QPushButton::clicked, this, &MainWindow::openSettings);

    bl->addWidget(m_startButton);
    bl->addWidget(m_stopButton);
    bl->addStretch(1);
    bl->addWidget(m_refreshButton);
    bl->addWidget(m_resetSortButton);
    bl->addWidget(m_settingsButton);
    tl->addLayout(bl);
    tl->addSpacing(15);

    m_tableWidget = new QTableWidget(p);
    m_tableWidget->setColumnCount(2);
    m_tableWidget->setHorizontalHeaderLabels({"Nome Serie", "Stato"});
    m_tableWidget->verticalHeader()->setVisible(false);
    m_tableWidget->setAlternatingRowColors(true);
    m_tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableWidget->setSortingEnabled(true);
    m_tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tableWidget->setColumnWidth(1, 230);
    m_tableWidget->setItemDelegateForColumn(1, new ProgressBarDelegate(this));
    connect(m_tableWidget, &QTableWidget::itemSelectionChanged, this, &MainWindow::onSeriesSelected);

    m_imageLabel = new QLabel(p);
    m_imageLabel->setFixedSize(220, 320);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet("border: 2px solid gray; border-radius: 6px;");
    m_imageLabel->setText("Nessuna Immagine");

    QHBoxLayout *dl = new QHBoxLayout();
    dl->addWidget(m_tableWidget);
    dl->addWidget(m_imageLabel);
    tl->addLayout(dl);

    m_logOutput = new QTextEdit(p);
    m_logOutput->setReadOnly(true);
    m_mainSplitter = new QSplitter(Qt::Vertical, p);
    m_mainSplitter->addWidget(tc);
    m_mainSplitter->addWidget(m_logOutput);
    m_mainSplitter->setStretchFactor(0, 3);
    m_mainSplitter->setStretchFactor(1, 1);
    l->addWidget(m_mainSplitter);

    m_overallStatusLabel = new QLabel("Pronto.", p);
    m_overallStatusLabel->setStyleSheet("font-size: 10pt; font-weight: bold;");
    l->addWidget(m_overallStatusLabel);
}

// =====================================================================
// LOGICA DOWNLOAD E AGGIORNAMENTI
// =====================================================================

void MainWindow::updateSeriesStatus(const QString& name, const QString& msg) {
    QString statusLower = msg.toLower();
    int newPriority = 1;
    bool isActive = statusLower.contains("%") || statusLower.contains("dl") || statusLower.contains("conv");

    if (isActive) {
        newPriority = 0;
    } else if (statusLower.contains("fatto") || statusLower.contains("✅")) {
        newPriority = 2;
    } else if (statusLower.contains("saltato") || statusLower.contains("🚫")) {
        newPriority = 3;
    }

    for (int i = 0; i < m_tableWidget->rowCount(); ++i) {
        if (m_tableWidget->item(i, 0)->text() == name) {
            auto *item = dynamic_cast<ProgressBarTableWidgetItem*>(m_tableWidget->item(i, 1));
            if (item) {
                int p = 0;
                QRegularExpression re("(\\d+)%");
                QRegularExpressionMatch m = re.match(msg);
                if (m.hasMatch()) p = m.captured(1).toInt();

                item->setText(isActive ? msg.split(" - ").at(0) : msg);
                item->setData(Qt::UserRole + 1, p);
                item->setData(Qt::UserRole + 2, isActive);
                item->setPriority(newPriority);
            }
            break;
        }
    }
    m_tableWidget->sortItems(1, Qt::AscendingOrder);
}

void MainWindow::startDownload() {
    if (m_seriesData.empty()) return;
    if (m_downloadThread && m_downloadThread->isRunning()) return;

    setUiStateForDownload(true);
    m_logOutput->clear();

    m_downloadThread = new QThread(this);
    m_downloadWorker = new DownloadWorker(m_seriesData, m_jsonFilePath.toStdString(), m_logFilePath.toStdString(), m_outputDir.toStdString());
    m_downloadWorker->moveToThread(m_downloadThread);

    connect(m_downloadThread, &QThread::started, m_downloadWorker, &DownloadWorker::run);
    connect(m_downloadWorker, &DownloadWorker::progress, this, &MainWindow::updateSeriesStatus);
    connect(m_downloadWorker, &DownloadWorker::error, this, &MainWindow::handleWorkerError);
    connect(m_downloadWorker, &DownloadWorker::finished, this, &MainWindow::handleSeriesFinished);
    connect(m_downloadWorker, &DownloadWorker::taskSkipped, this, &MainWindow::handleTaskSkipped);
    connect(m_downloadWorker, &DownloadWorker::overallStatus, this, &MainWindow::updateOverallStatus);
    connect(m_downloadWorker, &DownloadWorker::allWorkFinished, this, &MainWindow::onDownloadFinished);
    
    // Il worker si cancella da solo a fine lavoro, ma il thread lo gestiamo noi
    connect(m_downloadThread, &QThread::finished, m_downloadWorker, &QObject::deleteLater);

    m_downloadThread->start();
}

void MainWindow::onDownloadFinished() {
    setUiStateForDownload(false);
    updateOverallStatus(m_overallStatusLabel->text().contains("Interruzione") ? "Processo interrotto." : "Processo completato.");
    
    if (m_downloadThread) {
        m_downloadThread->quit();
        m_downloadThread->wait();
        m_downloadThread->deleteLater();
        m_downloadThread = nullptr; // Fondamentale!
        m_downloadWorker = nullptr;
    }
}

void MainWindow::setUiStateForDownload(bool in) {
    m_tableWidget->setSortingEnabled(!in);
    m_startButton->setEnabled(!in);
    m_stopButton->setEnabled(in);
    m_tabManagerBtn->setEnabled(!in);
    m_refreshButton->setEnabled(!in);
    m_resetSortButton->setEnabled(!in);
}

// =====================================================================
// UTILS E EVENTI
// =====================================================================

void MainWindow::loadSeriesDataIntoTable(int r) {
    if (m_downloadThread && m_downloadThread->isRunning()) return;
    try {
        m_seriesData = m_seriesRepository->loadSeriesData();
    } catch (...) {
        m_seriesData.clear();
    }
    populateTable(m_seriesData, r);
}

void MainWindow::populateTable(const std::vector<Core::Series>& list, int r, bool s) {
    m_tableWidget->setSortingEnabled(false);
    m_tableWidget->setRowCount(static_cast<int>(list.size()));

    for (size_t i = 0; i < list.size(); ++i) {
        m_tableWidget->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(list[i].name)));
        m_tableWidget->setItem(i, 1, new ProgressBarTableWidgetItem("In attesa", 3));
    }

    if (!list.empty() && r != -1) {
        int target = std::min(std::max(0, r), static_cast<int>(list.size()) - 1);
        m_tableWidget->setCurrentCell(target, 0);
        if (s) m_tableWidget->scrollToItem(m_tableWidget->item(target, 0));
        onSeriesSelected();
    }
    m_tableWidget->setSortingEnabled(true);
}

void MainWindow::resetTableSort() {
    m_tableWidget->setSortingEnabled(false);
    m_tableWidget->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    try {
        m_seriesData = m_seriesRepository->loadSeriesData();
    } catch (...) {
        m_seriesData.clear();
    }
    populateTable(m_seriesData);
    m_tableWidget->setSortingEnabled(true);
}

void MainWindow::onSeriesSelected() {
    int row = m_tableWidget->currentRow();
    if (row == -1) return;
    QTableWidgetItem *item = m_tableWidget->item(row, 0);
    if (!item) return;

    QString name = item->text();
    auto it = std::find_if(m_seriesData.begin(), m_seriesData.end(), [&](const Core::Series& s) {
        return QString::fromStdString(s.name) == name;
    });

    if (it != m_seriesData.end()) {
        std::filesystem::path imgPath = std::filesystem::path(it->path).parent_path() / "folder.jpg";
        if (std::filesystem::exists(imgPath)) {
            m_imageLabel->setPixmap(ImageCache::instance().get(QString::fromStdString(imgPath.string()), 220, 320));
        } else {
            m_imageLabel->clear();
            m_imageLabel->setText("No Image");
        }
    }
}

void MainWindow::handleWorkerError(const QString& n, const QString& e) {
    if (n == "GLOBAL" || n == "DEPENDENCIES") QMessageBox::critical(this, "Errore", e);
    updateSeriesStatus(n, "❌ Errore");
    m_logOutput->append("ERRORE [" + n + "]: " + e);
}

void MainWindow::handleSeriesFinished(const QString& n, const QString& p, double d, double c) {
    m_logOutput->append(QString("✅ %1 | DL: %2s | Conv: %3s").arg(p).arg(d).arg(c));
    updateSeriesStatus(n, "✅ Fatto");
    if (m_trayIcon) {
        m_trayIcon->showMessage("Completato", n + " scaricato.", QSystemTrayIcon::Information, 3000);
    }
}

void MainWindow::handleTaskSkipped(const QString& n, const QString& r) {
    m_logOutput->append("🚫 SKIPPED [" + n + "]: " + r);
    updateSeriesStatus(n, "🚫 Saltato");
}

void MainWindow::updateOverallStatus(const QString& s) {
    m_overallStatusLabel->setText(s);
    m_logOutput->append(s);
}

void MainWindow::onSeriesDataChanged() {
    loadSeriesDataIntoTable();
}

void MainWindow::switchView(int i) {
    if (!m_slider->isAnimating()) {
        m_tabDownloadBtn->setEnabled(false);
        m_tabManagerBtn->setEnabled(false);
        m_slider->slideToIndex(i);
    }
}

void MainWindow::stopDownload() {
    bool showWarning = m_settings->value("show_stop_warning", true).toBool();
    if (showWarning) {
        StopConfirmationDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted) {
            if (dialog.dontShowAgain()) {
                m_settings->setValue("show_stop_warning", false);
                m_settings->sync();
            }
            executeStopProcedure();
        }
    } else {
        executeStopProcedure();
    }
}

void MainWindow::executeStopProcedure() {
    if (m_downloadWorker) {
        m_stopButton->setEnabled(false);
        m_downloadWorker->requestStop();
    }
}

void MainWindow::refreshSeries() {
    loadSeriesDataIntoTable();
}

void MainWindow::openSettings() {
    SettingsDialog dialog(m_configManager.get(), m_settings.get(), this);
    if (dialog.exec() == QDialog::Accepted) {
        loadConfigPaths();
        m_seriesRepository = std::make_unique<Core::SeriesRepository>(m_jsonFilePath.toStdString());
        loadSeriesDataIntoTable();
    }
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

void MainWindow::restoreGeometryAndState() {
    if (m_settings->contains("geometry")) {
        restoreGeometry(m_settings->value("geometry").toByteArray());
    }
    if (m_settings->contains("splitter_sizes")) {
        m_mainSplitter->restoreState(m_settings->value("splitter_sizes").toByteArray());
    }
}

void MainWindow::closeEvent(QCloseEvent *e) {
    if (m_downloadThread && m_downloadThread->isRunning()) {
        bool showWarning = m_settings->value("show_close_warning", true).toBool();
        
        if (showWarning) {
            CloseConfirmationDialog d(false, this);
            if (d.exec() != QDialog::Accepted) {
                e->ignore();
                return;
            }
            if (d.dontShowAgain()) {
                m_settings->setValue("show_close_warning", false);
                m_settings->sync();
            }
        }

        // FASE CRITICA: Fermiamo il worker e ASPETTIAMO il thread
        executeStopProcedure(); 
        m_downloadThread->quit();
        
        // Aspetta fino a 3 secondi che il thread finisca pulito
        if (!m_downloadThread->wait(3000)) {
            // Se non risponde (es. loop bloccato), forziamo l'uscita
            m_downloadThread->terminate();
            m_downloadThread->wait();
        }
    }

    m_settings->setValue("geometry", saveGeometry());
    m_settings->setValue("splitter_sizes", m_mainSplitter->saveState());
    m_settings->sync();
    
    QMainWindow::closeEvent(e);
}

void MainWindow::changeEvent(QEvent *e) {
    if (e->type() == QEvent::PaletteChange) {
        m_themeDebounceTimer->start(500);
    }
    QMainWindow::changeEvent(e);
}

void MainWindow::applyThemeOnEvent() {
    applyTheme();
}

void MainWindow::initTrayIcon() {
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    m_trayIcon->show();
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayIconActivated);
}

void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason r) {
    if (r == QSystemTrayIcon::Trigger) {
        if (isVisible()) {
            hide();
        } else {
            showNormal();
            activateWindow();
        }
    }
}

void MainWindow::applyTheme() {
    qApp->setStyleSheet(qApp->palette().color(QPalette::Window).lightness() < 128 ? DARK_THEME_QSS : LIGHT_THEME_QSS);
}

} // namespace Gui