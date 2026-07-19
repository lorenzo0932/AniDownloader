#include "gui/SeriesEditorDialog.hpp"
#include "gui/ImageCache.hpp"
#include "gui/ScaleHelper.hpp"
#include "scrapers/ScraperUtils.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QGroupBox>
#include <QDir>
#include <QTimer>
#include <QStyle>
#include <filesystem>

namespace Gui {

SeriesEditorDialog::SeriesEditorDialog(const Core::Series& seriesData, bool isNew, QWidget *parent)
    : QDialog(parent), m_seriesData(seriesData), m_isNew(isNew)
{
    QString title = isNew ? "Aggiungi Nuova Serie" : QString("Modifica: %1").arg(QString::fromStdString(seriesData.name));
    setWindowTitle(title);
    setMinimumSize(ScaleHelper::px(500), ScaleHelper::px(600));

    initUi();

    m_fetchNameTimer = new QTimer(this);
    m_fetchNameTimer->setSingleShot(true);
    m_fetchNameTimer->setInterval(1200);
    connect(m_fetchNameTimer, &QTimer::timeout, this, &SeriesEditorDialog::autoFetchName);

    connect(m_seriesPageUrlInput, &QLineEdit::textChanged, this, [this](const QString& text) {
        Q_UNUSED(text);
        if (!m_userEditedName && m_nameInput->text().isEmpty()) {
            m_fetchNameTimer->start();
        }
    });

    connect(m_nameInput, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (!text.isEmpty()) m_userEditedName = true;
        m_fetchNameTimer->stop();
    });

    connect(m_fetchNameBtn, &QPushButton::clicked, this, &SeriesEditorDialog::manualFetchName);

    QTimer::singleShot(0, this, &SeriesEditorDialog::populateFields);
}

void SeriesEditorDialog::initUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    m_imageLabel = new QLabel(this);
    m_imageLabel->setFixedSize(ScaleHelper::px(220), ScaleHelper::px(320)); 
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet(QString("border: %1px solid #444; background-color: #121212; border-radius: %2px;").arg(ScaleHelper::px(1)).arg(ScaleHelper::px(4)));
    
    QHBoxLayout *imageContainer = new QHBoxLayout();
    imageContainer->addStretch();
    imageContainer->addWidget(m_imageLabel);
    imageContainer->addStretch();
    mainLayout->addLayout(imageContainer);

    QWidget *formWidget = new QWidget(this);
    QFormLayout *formLayout = new QFormLayout(formWidget);

    QGroupBox *serviceGroupBox = new QGroupBox("Servizio di Download", this);
    QHBoxLayout *serviceLayout = new QHBoxLayout(serviceGroupBox);
    m_rbAnimeW = new QRadioButton("AnimeW Scraper", serviceGroupBox);
    m_rbAnimeW->setToolTip("Scraper per AnimeWorld (animeworld.ac)");
    m_rbAnimeU = new QRadioButton("AnimeU Scraper", serviceGroupBox);
    m_rbAnimeU->setToolTip("Scraper per AnimeUnity (animeunity.to)");
    m_serviceButtonGroup = new QButtonGroup(this);
    m_serviceButtonGroup->addButton(m_rbAnimeW, 1);
    m_serviceButtonGroup->addButton(m_rbAnimeU, 2);
    serviceLayout->addWidget(m_rbAnimeW);
    serviceLayout->addWidget(m_rbAnimeU);
    formLayout->addRow(serviceGroupBox);

    QWidget *nameContainer = new QWidget(this);
    QHBoxLayout *nameLayout = new QHBoxLayout(nameContainer);
    nameLayout->setContentsMargins(0, 0, 0, 0);
    m_nameInput = new QLineEdit(nameContainer);
    m_nameInput->setToolTip("Nome leggibile della serie (recuperato automaticamente dall'URL)");
    m_fetchNameBtn = new QPushButton(nameContainer);
    m_fetchNameBtn->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    int fetchIconSize = ScaleHelper::px(14);
    m_fetchNameBtn->setIconSize(QSize(fetchIconSize, fetchIconSize));
    m_fetchNameBtn->setToolTip("Recupera il nome dalla pagina web");
    m_fetchNameBtn->setFixedSize(ScaleHelper::px(30), ScaleHelper::px(24));
    m_fetchNameBtn->setObjectName("fetchNameButton");
    nameLayout->addWidget(m_nameInput);
    nameLayout->addWidget(m_fetchNameBtn);
    formLayout->addRow("Nome:", nameContainer);

    QWidget *pathContainer = new QWidget(this);
    QHBoxLayout *pathLayout = new QHBoxLayout(pathContainer);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    m_pathInput = new QLineEdit(pathContainer);
    m_pathInput->setToolTip("Cartella locale dove salvare gli episodi");
    QPushButton *pathBrowseBtn = new QPushButton("Sfoglia...", pathContainer);
    pathBrowseBtn->setToolTip("Seleziona la cartella di destinazione");
    
    connect(pathBrowseBtn, &QPushButton::clicked, this, &SeriesEditorDialog::browseSeriesPath);
    connect(m_pathInput, &QLineEdit::textChanged, this, &SeriesEditorDialog::loadPoster);
    
    pathLayout->addWidget(m_pathInput);
    pathLayout->addWidget(pathBrowseBtn);
    formLayout->addRow("Percorso Cartella:", pathContainer);

    m_seriesPageUrlInput = new QLineEdit(this);
    m_seriesPageUrlInput->setToolTip("URL della pagina della serie sul sito di streaming");
    formLayout->addRow("URL Pagina Serie:", m_seriesPageUrlInput);

    m_continueCheckbox = new QCheckBox(this);
    m_continueCheckbox->setToolTip("Continua la numerazione dagli episodi già scaricati");
    formLayout->addRow("Continua numerazione:", m_continueCheckbox);

    m_highPriorityCheckbox = new QCheckBox(this);
    m_highPriorityCheckbox->setToolTip("Esegui questa serie prima delle altre");
    formLayout->addRow("Alta Priorit\u00e0:", m_highPriorityCheckbox);

    m_passedEpisodesInput = new QSpinBox(this);
    m_passedEpisodesInput->setRange(0, 9999);
    m_passedEpisodesInput->setToolTip("Numero di episodi già visti su altri servizi");
    formLayout->addRow("Episodi Passati:", m_passedEpisodesInput);

    mainLayout->addWidget(formWidget);
    mainLayout->addStretch();

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    m_deleteButton = new QPushButton("Elimina Serie", this);
    m_deleteButton->setObjectName("dangerButton");
    m_deleteButton->setToolTip("Elimina questa serie dall'elenco");
    connect(m_deleteButton, &QPushButton::clicked, this, &SeriesEditorDialog::deleteSeries);
    if (m_isNew) m_deleteButton->hide();

    QPushButton *cancelBtn = new QPushButton("Annulla", this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    QPushButton *saveBtn = new QPushButton("Salva Modifiche", this);
    saveBtn->setObjectName("primaryButton");
    saveBtn->setDefault(true);
    connect(saveBtn, &QPushButton::clicked, this, &SeriesEditorDialog::saveChanges);

    buttonLayout->addWidget(m_deleteButton);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(saveBtn);
    mainLayout->addLayout(buttonLayout);
}

void SeriesEditorDialog::populateFields() {
    m_nameInput->setText(QString::fromStdString(m_seriesData.name));
    m_pathInput->setText(QString::fromStdString(m_seriesData.path));
    m_seriesPageUrlInput->setText(QString::fromStdString(m_seriesData.seriesPageUrl));
    
    m_continueCheckbox->setChecked(m_isNew ? false : m_seriesData.continueSeries);
    m_highPriorityCheckbox->setChecked(m_seriesData.isHighPriority);
    m_passedEpisodesInput->setValue(m_seriesData.passedEpisodes);

    if (m_seriesData.service == "animeU_scraper") m_rbAnimeU->setChecked(true);
    else m_rbAnimeW->setChecked(true);

    loadPoster();
}

void Gui::SeriesEditorDialog::loadPoster() {
    std::string pathStr = m_pathInput->text().toStdString();
    if (pathStr.empty()) {
        m_imageLabel->clear();
        m_imageLabel->setText("Nessun Percorso");
        return;
    }

    std::filesystem::path seriesPath(Core::ScraperUtils::expandTilde(pathStr));
    std::filesystem::path imgPath = seriesPath / "folder.jpg";
    if (!std::filesystem::exists(imgPath)) imgPath = seriesPath.parent_path() / "folder.jpg";

    if (std::filesystem::exists(imgPath)) {
        m_imageLabel->setPixmap(ImageCache::instance().get(QString::fromStdString(imgPath.string()), 220, 320));
    } else {
        m_imageLabel->clear();
        m_imageLabel->setText("Locandina\nnon trovata");
    }
}

void SeriesEditorDialog::browseSeriesPath() {
    QString startDir = m_pathInput->text().isEmpty() ? QDir::homePath() : m_pathInput->text();
    QString selectedDir = QFileDialog::getExistingDirectory(this, "Seleziona Cartella Serie", startDir);
    if (!selectedDir.isEmpty()) {
        m_pathInput->setText(selectedDir);
    }
}

void SeriesEditorDialog::autoFetchName() {
    QString url = m_seriesPageUrlInput->text().trimmed();
    if (url.isEmpty() || m_userEditedName) return;
    performNameFetch(url);
}

void SeriesEditorDialog::manualFetchName() {
    QString url = m_seriesPageUrlInput->text().trimmed();
    if (url.isEmpty()) return;

    if (QMessageBox::question(this, "Recupera Nome",
        "Vuoi recuperare il nome della serie dalla pagina web?") == QMessageBox::Yes) {
        m_userEditedName = false;
        performNameFetch(url);
    }
}

void SeriesEditorDialog::performNameFetch(const QString& url) {
    if (m_fetchInProgress) return;
    m_fetchInProgress = true;
    m_fetchNameBtn->setEnabled(false);
    m_nameInput->setPlaceholderText("Recupero nome...");

    std::string urlStr = url.toStdString();
    m_fetchFuture = std::async(std::launch::async, [urlStr]() {
        return Core::ScraperUtils::fetchSeriesNameFromUrl(urlStr);
    });

    QTimer *pollTimer = new QTimer(this);
    connect(pollTimer, &QTimer::timeout, this, [this, pollTimer]() {
        if (m_fetchFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            pollTimer->stop();
            pollTimer->deleteLater();
            std::string name = m_fetchFuture.get();
            m_fetchInProgress = false;
            m_fetchNameBtn->setEnabled(true);
            m_nameInput->setPlaceholderText("");
            if (!name.empty() && m_nameInput->text().isEmpty()) {
                m_nameInput->setText(QString::fromStdString(name));
            }
        }
    });
    pollTimer->start(50);
}

void SeriesEditorDialog::saveChanges() {
    if (m_nameInput->text().trimmed().isEmpty() || m_pathInput->text().trimmed().isEmpty() || m_seriesPageUrlInput->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Campi Mancanti", "Nome, Percorso e URL sono obbligatori.");
        return;
    }

    m_resultData.name = m_nameInput->text().trimmed().toStdString();
    m_resultData.path = m_pathInput->text().trimmed().toStdString();
    m_resultData.seriesPageUrl = m_seriesPageUrlInput->text().trimmed().toStdString();
    m_resultData.service = (m_serviceButtonGroup->checkedId() == 2) ? "animeU_scraper" : "animeW_scraper";
    m_resultData.continueSeries = m_continueCheckbox->isChecked();
    m_resultData.isHighPriority = m_highPriorityCheckbox->isChecked();
    m_resultData.passedEpisodes = m_passedEpisodesInput->value();
    
    accept();
}

void SeriesEditorDialog::deleteSeries() {
    if (QMessageBox::question(this, "Conferma Eliminazione", "Vuoi eliminare questa serie dal database?") == QMessageBox::Yes) {
        m_isDeleted = true;
        accept();
    }
}

} // namespace Gui
