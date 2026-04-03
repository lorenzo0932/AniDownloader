#include "gui/SettingsDialog.hpp"
#include <QFileDialog>
#include <QStyle>
#include <QMessageBox>

namespace Gui {

SettingsDialog::SettingsDialog(Config::AppConfigManager *configManager, QSettings *settings, QWidget *parent)
    : QDialog(parent), m_configManager(configManager), m_qSettings(settings)
{
    setWindowTitle("Impostazioni AniDownloader");
    setModal(true);
    resize(600, 550);
    setMinimumWidth(500);

    m_initialJsonPath = m_configManager->get<std::string>("json_file_path", "");
    m_initialOutputDir = m_configManager->get<std::string>("output_dir", "");

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget *contentWidget = new QWidget();
    QVBoxLayout *contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setSpacing(20);
    contentLayout->setContentsMargins(20, 20, 20, 20);

    initPathsSection(contentLayout);
    initVideoSection(contentLayout);
    initAppSection(contentLayout);

    contentLayout->addStretch();
    scrollArea->setWidget(contentWidget);
    mainLayout->addWidget(scrollArea);

    initButtons(mainLayout);
}

void SettingsDialog::initPathsSection(QVBoxLayout *contentLayout) {
    QGroupBox *group = new QGroupBox("Gestione File e Percorsi", this);
    QVBoxLayout *layout = new QVBoxLayout(group);
    layout->setSpacing(10);

    layout->addWidget(new QLabel("File Database Serie (JSON):"));
    QHBoxLayout *jsonRow = new QHBoxLayout();
    m_jsonPathEdit = new QLineEdit(QString::fromStdString(m_initialJsonPath));
    QPushButton *jsonBtn = new QPushButton("...");
    jsonBtn->setFixedWidth(40);
    connect(jsonBtn, &QPushButton::clicked, this, &SettingsDialog::browseJson);
    jsonRow->addWidget(m_jsonPathEdit);
    jsonRow->addWidget(jsonBtn);
    layout->addLayout(jsonRow);

    layout->addWidget(new QLabel("Cartella di Destinazione (Output):"));
    QHBoxLayout *outRow = new QHBoxLayout();
    m_outputDirEdit = new QLineEdit(QString::fromStdString(m_initialOutputDir));
    QPushButton *outBtn = new QPushButton("...");
    outBtn->setFixedWidth(40);
    connect(outBtn, &QPushButton::clicked, this, &SettingsDialog::browseOutput);
    outRow->addWidget(m_outputDirEdit);
    outRow->addWidget(outBtn);
    layout->addLayout(outRow);

    contentLayout->addWidget(group);
}

void SettingsDialog::initVideoSection(QVBoxLayout *contentLayout) {
    QGroupBox *group = new QGroupBox("Codifica e Prestazioni Video", this);
    QVBoxLayout *layout = new QVBoxLayout(group);
    layout->setSpacing(10);

    m_h265Checkbox = new QCheckBox("Abilita compressione H.265 (HEVC)");
    m_h265Checkbox->setChecked(m_configManager->get<bool>("convert_to_h265", true));
    layout->addWidget(m_h265Checkbox);

    QLabel *h265Desc = new QLabel("Riduce le dimensioni del file (~50%) mantenendo la qualità.");
    h265Desc->setWordWrap(true);
    h265Desc->setStyleSheet("color: #b0b0b0; font-size: 9pt; margin-left: 24px;");
    layout->addWidget(h265Desc);

    QFrame *line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    QLabel *chunkTitle = new QLabel("Elaborazione Parallela (Chunk Splitting)");
    chunkTitle->setStyleSheet("font-weight: bold;");
    layout->addWidget(chunkTitle);

    layout->addWidget(new QLabel("Divide il video in segmenti per utilizzare più core della CPU contemporaneamente."));

    QHBoxLayout *chunkCtrlLayout = new QHBoxLayout();
    chunkCtrlLayout->addWidget(new QLabel("Numero di Chunk:"));
    m_chunkSpin = new QSpinBox();
    m_chunkSpin->setRange(0, 32);                      // PERMETTE LO 0
    m_chunkSpin->setSpecialValueText("Auto");          // MOSTRA "Auto" AL POSTO DI 0
    m_chunkSpin->setValue(m_configManager->get<int>("num_chunks", 0));
    m_chunkSpin->setFixedWidth(80);
    chunkCtrlLayout->addWidget(m_chunkSpin);
    chunkCtrlLayout->addStretch();
    layout->addLayout(chunkCtrlLayout);

    QWidget *warningContainer = new QWidget();
    warningContainer->setObjectName("warningLabel");
    QHBoxLayout *warningLayout = new QHBoxLayout(warningContainer);
    
    QLabel *warningIconLbl = new QLabel();
    warningIconLbl->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(32, 32));
    warningLayout->addWidget(warningIconLbl, 0, Qt::AlignTop);

    QLabel *warningText = new QLabel(
        "<b>Attenzione alle Prestazioni:</b><br>"
        "• <b>H.265:</b> Richiede molta potenza di calcolo.<br>"
        "• <b>Chunk > 1:</b> Aumenta drasticamente l'uso di RAM e CPU.<br>"
        "• <b>Auto:</b> Adatta automaticamente le risorse in base al tuo PC."
    );
    warningText->setWordWrap(true);
    warningLayout->addWidget(warningText, 1);
    layout->addWidget(warningContainer);

    contentLayout->addWidget(group);
}

void SettingsDialog::initAppSection(QVBoxLayout *contentLayout) {
    QGroupBox *group = new QGroupBox("Comportamento e Avvisi", this);
    QVBoxLayout *layout = new QVBoxLayout(group);
    layout->setSpacing(10);

    m_confirmStopCheckbox = new QCheckBox("Mostra conferma prima di interrompere un download");
    m_confirmStopCheckbox->setChecked(m_qSettings->value("show_stop_warning", true).toBool());
    layout->addWidget(m_confirmStopCheckbox);

    m_confirmCloseCheckbox = new QCheckBox("Mostra conferma prima di chiudere l'app (durante un download)");
    m_confirmCloseCheckbox->setChecked(m_qSettings->value("show_close_warning", true).toBool());
    layout->addWidget(m_confirmCloseCheckbox);

    QFrame *line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    m_autoCleanupCheckbox = new QCheckBox("Pulizia Automatica dei file parziali");
    m_autoCleanupCheckbox->setChecked(m_configManager->get<bool>("auto_cleanup_on_close", true));
    layout->addWidget(m_autoCleanupCheckbox);

    QLabel *cleanupDesc = new QLabel("Rimuove automaticamente i file temporanei (.aria2) e i segmenti video in caso di interruzione o chiusura forzata.");
    cleanupDesc->setWordWrap(true);
    cleanupDesc->setStyleSheet("color: #b0b0b0; font-size: 9pt; margin-left: 24px;");
    layout->addWidget(cleanupDesc);

    contentLayout->addWidget(group);
}

void SettingsDialog::initButtons(QVBoxLayout *mainLayout) {
    QWidget *container = new QWidget();
    container->setObjectName("settingsFooter");
    QHBoxLayout *btnLayout = new QHBoxLayout(container);
    btnLayout->setContentsMargins(20, 15, 20, 15);

    QPushButton *saveBtn = new QPushButton("Salva Modifiche");
    saveBtn->setObjectName("primaryButton");
    connect(saveBtn, &QPushButton::clicked, this, &SettingsDialog::saveSettings);

    QPushButton *cancelBtn = new QPushButton("Annulla");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(saveBtn);
    mainLayout->addWidget(container);
}

void SettingsDialog::browseJson() {
    QString selectedFile = QFileDialog::getOpenFileName(this, "Seleziona File Serie", m_jsonPathEdit->text(), "JSON Files (*.json)");
    if (!selectedFile.isEmpty()) m_jsonPathEdit->setText(selectedFile);
}

void SettingsDialog::browseOutput() {
    QString selectedDir = QFileDialog::getExistingDirectory(this, "Seleziona Cartella Output", m_outputDirEdit->text());
    if (!selectedDir.isEmpty()) m_outputDirEdit->setText(selectedDir);
}

void SettingsDialog::saveSettings() {
    std::string newJsonPath = m_jsonPathEdit->text().toStdString();
    std::string newOutputDir = m_outputDirEdit->text().toStdString();

    m_configManager->set("json_file_path", newJsonPath);
    m_configManager->set("output_dir", newOutputDir);

    if (newJsonPath != m_initialJsonPath) m_pathsChanged = true;
    if (newOutputDir != m_initialOutputDir) m_pathsChanged = true;

    m_configManager->set("convert_to_h265", m_h265Checkbox->isChecked());
    m_configManager->set("num_chunks", m_chunkSpin->value());
    m_qSettings->setValue("show_stop_warning", m_confirmStopCheckbox->isChecked());
    m_qSettings->setValue("show_close_warning", m_confirmCloseCheckbox->isChecked());
    m_configManager->set("auto_cleanup_on_close", m_autoCleanupCheckbox->isChecked());

    accept();
}

} // namespace Gui
