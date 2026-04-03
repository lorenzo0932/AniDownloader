#include "gui/SeriesEditorDialog.hpp"
#include "scrapers/ScraperUtils.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QGroupBox>
#include <QDir>
#include <filesystem>

namespace Gui {

SeriesEditorDialog::SeriesEditorDialog(const Core::Series& seriesData, bool isNew, QWidget *parent)
    : QDialog(parent), m_seriesData(seriesData), m_isNew(isNew)
{
    QString title = isNew ? "Aggiungi Nuova Serie" : QString("Modifica: %1").arg(QString::fromStdString(seriesData.name));
    setWindowTitle(title);
    setMinimumSize(500, 600);

    initUi();
    populateFields();
}

void SeriesEditorDialog::initUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    m_imageLabel = new QLabel("Locandina non trovata", this);
    // Impostiamo una dimensione fissa o minima per evitare problemi di scaling all'avvio
    m_imageLabel->setFixedSize(220, 320); 
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet("border: 1px solid #444; background-color: #121212; border-radius: 4px;");
    
    // Centriamo l'immagine orizzontalmente
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
    m_rbAnimeU = new QRadioButton("AnimeU Scraper", serviceGroupBox);
    m_serviceButtonGroup = new QButtonGroup(this);
    m_serviceButtonGroup->addButton(m_rbAnimeW, 1);
    m_serviceButtonGroup->addButton(m_rbAnimeU, 2);
    m_serviceButtonGroup->setExclusive(true);
    serviceLayout->addWidget(m_rbAnimeW);
    serviceLayout->addWidget(m_rbAnimeU);
    formLayout->addRow(serviceGroupBox);

    m_nameInput = new QLineEdit(this);
    formLayout->addRow("Nome:", m_nameInput);

    QWidget *pathContainer = new QWidget(this);
    QHBoxLayout *pathLayout = new QHBoxLayout(pathContainer);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    m_pathInput = new QLineEdit(pathContainer);
    QPushButton *pathBrowseBtn = new QPushButton("Sfoglia...", pathContainer);
    connect(pathBrowseBtn, &QPushButton::clicked, this, &SeriesEditorDialog::browseSeriesPath);
    // Aggiorna la locandina appena il percorso cambia
    connect(m_pathInput, &QLineEdit::textChanged, this, &SeriesEditorDialog::loadPoster);
    
    pathLayout->addWidget(m_pathInput);
    pathLayout->addWidget(pathBrowseBtn);
    formLayout->addRow("Percorso Cartella:", pathContainer);

    m_seriesPageUrlInput = new QLineEdit(this);
    formLayout->addRow("URL Pagina Serie:", m_seriesPageUrlInput);

    m_continueCheckbox = new QCheckBox(this);
    formLayout->addRow("Continua numerazione:", m_continueCheckbox);

    m_highPriorityCheckbox = new QCheckBox(this);
    formLayout->addRow("Alta Priorità:", m_highPriorityCheckbox);

    m_passedEpisodesInput = new QSpinBox(this);
    m_passedEpisodesInput->setRange(0, 9999);
    formLayout->addRow("Episodi Passati:", m_passedEpisodesInput);

    mainLayout->addWidget(formWidget);
    mainLayout->addStretch();

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    m_deleteButton = new QPushButton("Elimina Serie", this);
    m_deleteButton->setObjectName("dangerButton");
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
    
    if (m_isNew) {
        m_continueCheckbox->setChecked(false);
    } else {
        m_continueCheckbox->setChecked(m_seriesData.continueSeries);
    }
    
    m_highPriorityCheckbox->setChecked(m_seriesData.isHighPriority);
    m_passedEpisodesInput->setValue(m_seriesData.passedEpisodes);

    if (m_seriesData.service == "animeU_scraper") m_rbAnimeU->setChecked(true);
    else m_rbAnimeW->setChecked(true);

    loadPoster();
}

void SeriesEditorDialog::loadPoster() {
    std::string pathStr = m_pathInput->text().toStdString();
    if (pathStr.empty()) {
        m_imageLabel->clear();
        m_imageLabel->setText("Nessun percorso");
        return;
    }

    // 1. ESPANDI TILDE (Cruciale per trovare il file)
    std::string expandedPath = Core::ScraperUtils::expandTilde(pathStr);
    std::filesystem::path seriesPath(expandedPath);

    // 2. Cerchiamo 'folder.jpg' sia dentro la cartella che nella cartella padre
    // (A seconda di come l'utente organizza la libreria)
    std::filesystem::path imgPath = seriesPath / "folder.jpg";
    if (!std::filesystem::exists(imgPath)) {
        imgPath = seriesPath.parent_path() / "folder.jpg";
    }

    if (std::filesystem::exists(imgPath)) {
        QPixmap pixmap(QString::fromStdString(imgPath.string()));
        if (!pixmap.isNull()) {
            m_imageLabel->setPixmap(pixmap.scaled(m_imageLabel->size(), 
                                                  Qt::KeepAspectRatio, 
                                                  Qt::SmoothTransformation));
            return;
        }
    }

    m_imageLabel->clear();
    m_imageLabel->setText("Locandina non trovata");
}

void SeriesEditorDialog::browseSeriesPath() {
    QString startDir = m_pathInput->text().isEmpty() ? QDir::homePath() : m_pathInput->text();
    QString selectedDir = QFileDialog::getExistingDirectory(this, "Seleziona Cartella Serie", startDir);
    if (!selectedDir.isEmpty()) {
        m_pathInput->setText(selectedDir);
        // loadPoster viene chiamata automaticamente via segnale textChanged
    }
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