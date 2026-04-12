#ifndef SERIESEDITORDIALOG_HPP
#define SERIESEDITORDIALOG_HPP

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QLabel>
#include "core/Series.hpp"

namespace Gui {

    class SeriesEditorDialog : public QDialog {
        Q_OBJECT

    public:
        explicit SeriesEditorDialog(const Core::Series& seriesData, bool isNew, QWidget *parent = nullptr);

        bool isDeleted() const { return m_isDeleted; }
        Core::Series resultData() const { return m_resultData; }

    private slots:
        void browseSeriesPath();
        void saveChanges();
        void deleteSeries();
        void loadPoster(); // Caricamento diretto istantaneo

    private:
        void initUi();
        void populateFields();

        Core::Series m_seriesData;
        Core::Series m_resultData;
        bool m_isNew;
        bool m_isDeleted = false;

        QLabel *m_imageLabel;
        QRadioButton *m_rbAnimeW;
        QRadioButton *m_rbAnimeU;
        QButtonGroup *m_serviceButtonGroup;
        QLineEdit *m_nameInput;
        QLineEdit *m_pathInput;
        QLineEdit *m_seriesPageUrlInput;
        QCheckBox *m_continueCheckbox;
        QCheckBox *m_highPriorityCheckbox;
        QSpinBox *m_passedEpisodesInput;
        QPushButton *m_deleteButton;
    };

}

#endif // SERIESEDITORDIALOG_HPP