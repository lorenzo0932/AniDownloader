#include "gui/SeriesManagerDialog.hpp"
#include "gui/SeriesEditorDialog.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QApplication>
#include <QScreen>
#include <QStyle>
#include <filesystem>
#include <algorithm>

namespace Gui {

SeriesManagerDialog::SeriesManagerDialog(Core::SeriesRepository *repository, QWidget *parent)
    : QDialog(parent), m_seriesRepository(repository)
{
    setWindowTitle("Gestisci Serie");
    QScreen *screen = QGuiApplication::primaryScreen();
    int windowWidth = 1200;
    int windowHeight = 600;
    QRect screenGeometry = screen->geometry();
    setGeometry((screenGeometry.width() - windowWidth) / 2, (screenGeometry.height() - windowHeight) / 2, windowWidth, windowHeight);
    setMinimumSize(700, 500);

    initUi();
    loadSeriesData();
}

void SeriesManagerDialog::initUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QHBoxLayout *displayLayout = new QHBoxLayout();
    m_tableWidget = new QTableWidget(this);
    m_tableWidget->setColumnCount(5);
    m_tableWidget->setHorizontalHeaderLabels({"Nome", "Percorso", "URL Pagina Serie", "Continua", "Ep. Passati"});
    
    QHeaderView *header = m_tableWidget->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    header->setSectionResizeMode(1, QHeaderView::Stretch);
    header->setSectionResizeMode(2, QHeaderView::Stretch);
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(4, QHeaderView::ResizeToContents);

    m_tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableWidget->setSortingEnabled(true);
    m_tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_tableWidget, &QTableWidget::itemSelectionChanged, this, &SeriesManagerDialog::onSeriesSelected);
    connect(m_tableWidget, &QTableWidget::doubleClicked, this, &SeriesManagerDialog::openSeriesEditor);
    
    displayLayout->addWidget(m_tableWidget);

    m_imageLabel = new QLabel("Seleziona una serie", this);
    m_imageLabel->setFixedSize(200, 300);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet("border: 1px solid #ccc; background-color: #f0f0f0; color: black;");
    displayLayout->addWidget(m_imageLabel);
    
    mainLayout->addLayout(displayLayout);

    QHBoxLayout *controlLayout = new QHBoxLayout();
    controlLayout->addWidget(new QLabel("Cerca:"));
    m_searchInput = new QLineEdit(this);
    m_searchInput->setPlaceholderText("Cerca per nome...");
    connect(m_searchInput, &QLineEdit::textChanged, this, &SeriesManagerDialog::filterSeries);
    controlLayout->addWidget(m_searchInput);
    controlLayout->addStretch(1);

    QPushButton *resetSortBtn = new QPushButton("Ripristina Ordine", this);
    resetSortBtn->setIcon(style()->standardIcon(QStyle::SP_DialogResetButton));
    connect(resetSortBtn, &QPushButton::clicked, this, &SeriesManagerDialog::resetTableSort);
    controlLayout->addWidget(resetSortBtn);
    mainLayout->addLayout(controlLayout);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *addBtn = new QPushButton("Aggiungi Serie", this);
    addBtn->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;");
    connect(addBtn, &QPushButton::clicked, this, &SeriesManagerDialog::addSeries);

    QPushButton *editBtn = new QPushButton("Modifica Selezionata", this);
    connect(editBtn, &QPushButton::clicked, this, &SeriesManagerDialog::openSeriesEditor);

    QPushButton *removeBtn = new QPushButton("Rimuovi Selezionata", this);
    removeBtn->setStyleSheet("background-color: #f44336; color: white; font-weight: bold;");
    connect(removeBtn, &QPushButton::clicked, this, &SeriesManagerDialog::removeSelectedSeries);

    QPushButton *cancelBtn = new QPushButton("Annulla", this);
    connect(cancelBtn, &QPushButton::clicked, this, &SeriesManagerDialog::reject);

    QPushButton *saveBtn = new QPushButton("Salva e Chiudi", this);
    saveBtn->setDefault(true);
    connect(saveBtn, &QPushButton::clicked, this, &QDialog::accept);

    buttonLayout->addWidget(addBtn);
    buttonLayout->addWidget(editBtn);
    buttonLayout->addWidget(removeBtn);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(saveBtn);
    mainLayout->addLayout(buttonLayout);
}

void SeriesManagerDialog::loadSeriesData() {
    try {
        m_seriesData = m_seriesRepository->loadSeriesData();
        m_originalSeriesData = m_seriesData;
        filterSeries();
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Errore Caricamento", e.what());
    }
}

void SeriesManagerDialog::filterSeries() {
    QString searchText = m_searchInput->text().toLower();
    std::vector<Core::Series> filtered;
    for (const auto& s : m_seriesData) {
        if (QString::fromStdString(s.name).toLower().contains(searchText)) {
            filtered.push_back(s);
        }
    }
    populateTable(filtered);
}

void SeriesManagerDialog::populateTable(const std::vector<Core::Series>& data) {
    m_tableWidget->setSortingEnabled(false);
    m_tableWidget->setRowCount(0);
    m_tableWidget->setRowCount(static_cast<int>(data.size()));

    for (size_t i = 0; i < data.size(); ++i) {
        m_tableWidget->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(data[i].name)));
        m_tableWidget->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(data[i].path)));
        m_tableWidget->setItem(i, 2, new QTableWidgetItem(QString::fromStdString(data[i].seriesPageUrl)));
        m_tableWidget->setItem(i, 3, new QTableWidgetItem(data[i].continueSeries ? "Sì" : "No"));
        m_tableWidget->setItem(i, 4, new QTableWidgetItem(QString::number(data[i].passedEpisodes)));
    }
    m_tableWidget->setSortingEnabled(true);
    if (!data.empty()) m_tableWidget->selectRow(0);
}

void SeriesManagerDialog::onSeriesSelected() {
    auto selectedItems = m_tableWidget->selectedItems();
    if (selectedItems.isEmpty()) {
        m_imageLabel->clear();
        m_imageLabel->setText("Nessuna serie selezionata");
        return;
    }

    QString seriesName = m_tableWidget->item(m_tableWidget->currentRow(), 0)->text();
    auto it = std::find_if(m_seriesData.begin(), m_seriesData.end(), [&](const Core::Series& s) {
        return QString::fromStdString(s.name) == seriesName;
    });

    if (it != m_seriesData.end() && !it->path.empty()) {
        std::filesystem::path folderPath = std::filesystem::path(it->path).parent_path();
        std::filesystem::path imagePath = folderPath / "folder.jpg";
        if (std::filesystem::exists(imagePath)) {
            QPixmap pixmap(QString::fromStdString(imagePath.string()));
            if (!pixmap.isNull()) {
                m_imageLabel->setPixmap(pixmap.scaled(m_imageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                return;
            }
        }
    }
    m_imageLabel->clear();
    m_imageLabel->setText("Locandina non trovata");
}

void SeriesManagerDialog::addSeries() {
    SeriesEditorDialog editor({}, true, this);
    if (editor.exec() == QDialog::Accepted) {
        m_seriesData.push_back(editor.resultData());
        saveCurrentSeriesData();
        filterSeries();
    }
}

void SeriesManagerDialog::openSeriesEditor() {
    int row = m_tableWidget->currentRow();
    if (row == -1) return;

    QString name = m_tableWidget->item(row, 0)->text();
    auto it = std::find_if(m_seriesData.begin(), m_seriesData.end(), [&](const Core::Series& s) {
        return QString::fromStdString(s.name) == name;
    });

    if (it != m_seriesData.end()) {
        SeriesEditorDialog editor(*it, false, this);
        if (editor.exec() == QDialog::Accepted) {
            if (editor.isDeleted()) {
                m_seriesData.erase(it);
            } else {
                *it = editor.resultData();
            }
            saveCurrentSeriesData();
            filterSeries();
        }
    }
}

void SeriesManagerDialog::removeSelectedSeries() {
    int row = m_tableWidget->currentRow();
    if (row == -1) return;

    QString name = m_tableWidget->item(row, 0)->text();
    if (QMessageBox::question(this, "Conferma", "Rimuovere '" + name + "'?") == QMessageBox::Yes) {
        auto it = std::find_if(m_seriesData.begin(), m_seriesData.end(), [&](const Core::Series& s) {
            return QString::fromStdString(s.name) == name;
        });
        if (it != m_seriesData.end()) {
            m_seriesData.erase(it);
            saveCurrentSeriesData();
            filterSeries();
        }
    }
}

void SeriesManagerDialog::saveCurrentSeriesData() {
    try {
        m_seriesRepository->saveSeriesData(m_seriesData);
        m_originalSeriesData = m_seriesData;
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Errore Salvataggio", e.what());
    }
}

void SeriesManagerDialog::resetTableSort() {
    m_tableWidget->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    filterSeries();
}

void SeriesManagerDialog::reject() {
    m_seriesData = m_originalSeriesData;
    QDialog::reject();
}

} // namespace Gui
