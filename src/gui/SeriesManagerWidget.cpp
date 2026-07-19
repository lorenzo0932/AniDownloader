#include "gui/SeriesManagerWidget.hpp"
#include "gui/SeriesEditorDialog.hpp"
#include "gui/ImageCache.hpp"
#include "gui/ScaleHelper.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QStyle>
#include <filesystem>
#include <algorithm>

namespace Gui {

SeriesManagerWidget::SeriesManagerWidget(Core::SeriesRepository *repository, QWidget *parent)
    : QWidget(parent), m_seriesRepository(repository)
{
    initUi();

    // Poiché questo widget viene creato in background all'avvio dell'app,
    // carichiamo i dati in modo sincrono. Qualsiasi micro-attesa avverrà solo 
    // durante il primo avvio del programma, rendendo lo slide 100% istantaneo!
    loadSeriesData();
}

void SeriesManagerWidget::initUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(ScaleHelper::px(20), ScaleHelper::px(20), ScaleHelper::px(20), ScaleHelper::px(20));

    QHBoxLayout *displayLayout = new QHBoxLayout();
    m_tableWidget = new QTableWidget(this);
    m_tableWidget->setColumnCount(5);
    m_tableWidget->setHorizontalHeaderLabels({"Nome", "Percorso", "URL Pagina Serie", "Continua", "Ep. Passati"});
    
    QHeaderView *header = m_tableWidget->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    header->setSectionResizeMode(1, QHeaderView::Stretch);
    header->setSectionResizeMode(2, QHeaderView::Stretch);
    
    // OTTIMIZZAZIONE EXTREME: Rimossa l'istruzione ResizeToContents. 
    // Evita il ricalcolo per ogni singola cella. Impostiamo dimensioni fisse/interattive.
    header->setSectionResizeMode(3, QHeaderView::Interactive);
    header->setSectionResizeMode(4, QHeaderView::Interactive);
    m_tableWidget->setColumnWidth(3, ScaleHelper::px(80));
    m_tableWidget->setColumnWidth(4, ScaleHelper::px(100));

    m_tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableWidget->setSortingEnabled(true);
    m_tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_tableWidget, &QTableWidget::itemSelectionChanged, this, &SeriesManagerWidget::onSeriesSelected);
    connect(m_tableWidget, &QTableWidget::doubleClicked, this, &SeriesManagerWidget::openSeriesEditor);
    
    displayLayout->addWidget(m_tableWidget);

    m_imageLabel = new QLabel("Seleziona una serie", this);
    m_imageLabel->setFixedSize(ScaleHelper::px(200), ScaleHelper::px(300));
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet(QString("border: %1px solid #ccc; background-color: #f0f0f0; color: black; border-radius: %2px;").arg(ScaleHelper::px(1)).arg(ScaleHelper::px(4)));
    displayLayout->addWidget(m_imageLabel);
    
    mainLayout->addLayout(displayLayout);

    QHBoxLayout *controlLayout = new QHBoxLayout();
    controlLayout->addWidget(new QLabel("Cerca:"));
    m_searchInput = new QLineEdit(this);
    m_searchInput->setPlaceholderText("Cerca per nome...");
    m_searchInput->setToolTip("Filtra le serie per nome");
    connect(m_searchInput, &QLineEdit::textChanged, this, &SeriesManagerWidget::filterSeries);
    controlLayout->addWidget(m_searchInput);
    controlLayout->addStretch(1);

    QPushButton *resetSortBtn = new QPushButton("Ripristina Ordine", this);
    resetSortBtn->setIcon(style()->standardIcon(QStyle::SP_DialogResetButton));
    resetSortBtn->setToolTip("Ripristina l'ordine predefinito della tabella");
    connect(resetSortBtn, &QPushButton::clicked, this, &SeriesManagerWidget::resetTableSort);
    controlLayout->addWidget(resetSortBtn);
    mainLayout->addLayout(controlLayout);

    // BOTTONIERA INFERIORE (Rimossi i bottoni "Annulla" e "Salva/Chiudi")
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *addBtn = new QPushButton("Aggiungi Serie", this);
    addBtn->setObjectName("addSeriesButton");
    addBtn->setToolTip("Aggiungi una nuova serie all'elenco");
    connect(addBtn, &QPushButton::clicked, this, &SeriesManagerWidget::addSeries);

    QPushButton *editBtn = new QPushButton("Modifica Selezionata", this);
    editBtn->setObjectName("editSeriesButton");
    editBtn->setToolTip("Modifica i dati della serie selezionata");
    connect(editBtn, &QPushButton::clicked, this, &SeriesManagerWidget::openSeriesEditor);

    QPushButton *removeBtn = new QPushButton("Rimuovi Selezionata", this);
    removeBtn->setObjectName("removeSeriesButton");
    removeBtn->setToolTip("Rimuovi la serie selezionata dall'elenco");
    removeBtn->setToolTip("Rimuovi la serie selezionata dall'elenco");
    connect(removeBtn, &QPushButton::clicked, this, &SeriesManagerWidget::removeSelectedSeries);

    buttonLayout->addWidget(addBtn);
    buttonLayout->addWidget(editBtn);
    buttonLayout->addWidget(removeBtn);
    buttonLayout->addStretch(1);
    
    mainLayout->addLayout(buttonLayout);
}

void SeriesManagerWidget::onSeriesSelected() {
    int row = m_tableWidget->currentRow();
    
    // 1. Controllo fondamentale: se la riga non esiste o è invalida, pulisci e esci
    if (row < 0 || row >= m_tableWidget->rowCount()) {
        m_imageLabel->clear();
        m_imageLabel->setText("Nessuna Selezione");
        return;
    }

    QTableWidgetItem *item = m_tableWidget->item(row, 0);
    if (!item) return;

    QString name = item->text();
    auto it = std::find_if(m_seriesData.begin(), m_seriesData.end(), [&](const Core::Series& s) {
        return QString::fromStdString(s.name) == name;
    });

    // 2. Verifica che la serie esista ancora nel vettore prima di accedere al percorso
    if (it != m_seriesData.end() && !it->path.empty()) {
        std::filesystem::path imgPath = std::filesystem::path(it->path).parent_path() / "folder.jpg";
        if (std::filesystem::exists(imgPath)) {
            m_imageLabel->setPixmap(ImageCache::instance().get(QString::fromStdString(imgPath.string()), 200, 300));
        } else {
            m_imageLabel->setText("Locandina\nnon trovata");
        }
    } else {
        m_imageLabel->clear();
    }
}

void SeriesManagerWidget::loadSeriesData() {
    try {
        m_seriesData = m_seriesRepository->loadSeriesData();
        m_originalSeriesData = m_seriesData;
        filterSeries();
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Errore Caricamento", e.what());
    }
}

void SeriesManagerWidget::filterSeries() {
    QString searchText = m_searchInput->text().toLower();
    std::vector<Core::Series> filtered;
    for (const auto& s : m_seriesData) {
        if (QString::fromStdString(s.name).toLower().contains(searchText)) {
            filtered.push_back(s);
        }
    }
    populateTable(filtered);
}

void SeriesManagerWidget::populateTable(const std::vector<Core::Series>& seriesList) {
    // 3. Disabilita i segnali durante il popolamento per evitare che onSeriesSelected 
    // venga chiamata 50 volte mentre la tabella è in uno stato instabile
    m_tableWidget->blockSignals(true);
    
    m_tableWidget->setSortingEnabled(false);
    m_tableWidget->setRowCount(0);
    m_tableWidget->setRowCount(static_cast<int>(seriesList.size()));

    for (size_t i = 0; i < seriesList.size(); ++i) {
        m_tableWidget->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(seriesList[i].name)));
        m_tableWidget->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(seriesList[i].path)));
        m_tableWidget->setItem(i, 2, new QTableWidgetItem(QString::fromStdString(seriesList[i].seriesPageUrl)));
        m_tableWidget->setItem(i, 3, new QTableWidgetItem(seriesList[i].continueSeries ? "Sì" : "No"));
        m_tableWidget->setItem(i, 4, new QTableWidgetItem(QString::number(seriesList[i].passedEpisodes)));
    }
    
    m_tableWidget->setSortingEnabled(true);
    m_tableWidget->blockSignals(false); // Riabilita i segnali
    
    if (!seriesList.empty()) m_tableWidget->selectRow(0);
    else m_imageLabel->setText("Nessuna Serie");
}

void SeriesManagerWidget::addSeries() {
    SeriesEditorDialog editor({}, true, this);
    if (editor.exec() == QDialog::Accepted) {
        m_seriesData.push_back(editor.resultData());
        saveCurrentSeriesData();
        filterSeries();
    }
}

void SeriesManagerWidget::addSeriesWithUrl(const QString& url) {
    Core::Series s;
    s.seriesPageUrl = url.toStdString();
    s.continueSeries = true;
    SeriesEditorDialog editor(s, true, this);
    if (editor.exec() == QDialog::Accepted) {
        m_seriesData.push_back(editor.resultData());
        saveCurrentSeriesData();
        filterSeries();
    }
}

void SeriesManagerWidget::openSeriesEditor() {
    int row = m_tableWidget->currentRow();
    if (row == -1) return;

    QTableWidgetItem *item = m_tableWidget->item(row, 0);
    if (!item) return;

    QString name = item->text();
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
            saveCurrentSeriesData(); // Salva ed emette il segnale!
            filterSeries();
        }
    }
}

void SeriesManagerWidget::removeSelectedSeries() {
    int row = m_tableWidget->currentRow();
    if (row == -1) return;

    QTableWidgetItem *item = m_tableWidget->item(row, 0);
    if (!item) return;

    QString name = item->text();
    if (QMessageBox::question(this, "Conferma", "Rimuovere '" + name + "'?") == QMessageBox::Yes) {
        auto it = std::find_if(m_seriesData.begin(), m_seriesData.end(), [&](const Core::Series& s) {
            return QString::fromStdString(s.name) == name;
        });
        if (it != m_seriesData.end()) {
            m_seriesData.erase(it);
            saveCurrentSeriesData(); // Salva ed emette il segnale!
            filterSeries();
        }
    }
}

void SeriesManagerWidget::saveCurrentSeriesData() {
    try {
        m_seriesRepository->saveSeriesData(m_seriesData);
        m_originalSeriesData = m_seriesData;
        
        // FONDAMENTALE: Avvisa la MainWindow che i dati sono cambiati 
        // così la vista dei download si aggiorna in automatico.
        emit dataChanged(); 
        
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Errore Salvataggio", e.what());
    }
}

void SeriesManagerWidget::resetTableSort() {
    m_tableWidget->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    filterSeries();
}

} // namespace Gui