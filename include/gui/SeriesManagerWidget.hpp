#ifndef SERIESMANAGERWIDGET_HPP
#define SERIESMANAGERWIDGET_HPP

#include <QWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <vector>
#include "core/Series.hpp"
#include "core/SeriesRepository.hpp"

namespace Gui {

    class SeriesManagerWidget : public QWidget {
        Q_OBJECT

    public:
        explicit SeriesManagerWidget(Core::SeriesRepository *repository, QWidget *parent = nullptr);
        void addSeriesWithUrl(const QString& url);

    signals:
        // Questo segnale avviserà la MainWindow ogni volta che aggiungiamo/eliminiamo/modifichiamo
        // una serie, così la tabella principale si aggiornerà in background da sola!
        void dataChanged();

    private slots:
        void onSeriesSelected();
        void openSeriesEditor();
        void addSeries();
        void removeSelectedSeries();
        void filterSeries();
        void resetTableSort();

    private:
        void initUi();
        void loadSeriesData();
        void populateTable(const std::vector<Core::Series>& data);
        void saveCurrentSeriesData();

        Core::SeriesRepository *m_seriesRepository;
        std::vector<Core::Series> m_seriesData;
        
        // Lo teniamo per un eventuale ripristino se si vuole implementare un tasto "Annulla tutto"
        std::vector<Core::Series> m_originalSeriesData; 

        QTableWidget *m_tableWidget;
        QLabel *m_imageLabel;
        QLineEdit *m_searchInput;
    };

}

#endif // SERIESMANAGERWIDGET_HPP