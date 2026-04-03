#ifndef SERIESMANAGERDIALOG_HPP
#define SERIESMANAGERDIALOG_HPP

#include <QDialog>
#include <QTableWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <vector>
#include "core/Series.hpp"
#include "core/SeriesRepository.hpp"

namespace Gui {

    class SeriesManagerDialog : public QDialog {
        Q_OBJECT

    public:
        explicit SeriesManagerDialog(Core::SeriesRepository *repository, QWidget *parent = nullptr);

    protected:
        void reject() override;

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
        std::vector<Core::Series> m_originalSeriesData;

        QTableWidget *m_tableWidget;
        QLabel *m_imageLabel;
        QLineEdit *m_searchInput;
    };

}

#endif // SERIESMANAGERDIALOG_HPP
