#ifndef SETTINGSDIALOG_HPP
#define SETTINGSDIALOG_HPP

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QSpinBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QSettings>
#include <QScrollArea>
#include "config/AppConfigManager.hpp"

namespace Gui {

    class SettingsDialog : public QDialog {
        Q_OBJECT

    public:
        explicit SettingsDialog(Config::AppConfigManager *configManager, QSettings *settings, QWidget *parent = nullptr);

        bool pathsChanged() const { return m_pathsChanged; }

    private slots:
        void browseJson();
        void browseOutput();
        void saveSettings();

    private:
        void initPathsSection(QVBoxLayout *contentLayout);
        void initVideoSection(QVBoxLayout *contentLayout);
        void initAppSection(QVBoxLayout *contentLayout);
        void initButtons(QVBoxLayout *mainLayout);

        Config::AppConfigManager *m_configManager;
        QSettings *m_qSettings;

        QLineEdit *m_jsonPathEdit;
        QLineEdit *m_outputDirEdit;
        QCheckBox *m_h265Checkbox;
        QSpinBox *m_chunkSpin;
        QCheckBox *m_confirmStopCheckbox;
        QCheckBox *m_confirmCloseCheckbox;
        QCheckBox *m_autoCleanupCheckbox;

        std::string m_initialJsonPath;
        std::string m_initialOutputDir;
        bool m_pathsChanged = false;
    };

}

#endif // SETTINGSDIALOG_HPP