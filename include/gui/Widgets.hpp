#ifndef WIDGETS_HPP
#define WIDGETS_HPP

#include <QTableWidgetItem>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QDialog>
#include <QCheckBox>
#include <QPushButton>
#include <QTimer>
#include <QWidget>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QEasingCurve>
#include <QLabel>
#include <QPaintEvent>

namespace Gui {

    class ProgressBarTableWidgetItem : public QTableWidgetItem {
    public:
        explicit ProgressBarTableWidgetItem(const QString& text, int priority, int progress = 0, bool isActive = false, const QString& phase = "");
        QVariant data(int role) const override;
        void setData(int role, const QVariant& value) override;
        
        // Operatore fondamentale per l'ordinamento automatico della tabella
        bool operator<(const QTableWidgetItem& other) const override;
        
        void setPriority(int p) { m_priority = p; }
        int priority() const { return m_priority; }
        int progress() const { return m_progress; }
        bool isActive() const { return m_isActive; }
        QString phase() const { return m_phase; }

    private:
        int m_priority, m_progress;
        bool m_isActive;
        QString m_phase;
    };

    class ProgressBarDelegate : public QStyledItemDelegate {
        Q_OBJECT
    public:
        using QStyledItemDelegate::QStyledItemDelegate;
        void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    };

    class StopConfirmationDialog : public QDialog {
        Q_OBJECT
    public:
        explicit StopConfirmationDialog(QWidget *parent = nullptr);
        bool dontShowAgain() const;
    private slots: void updateTimer();
    private:
        QCheckBox *m_dontShowAgainCheckbox;
        QPushButton *m_okButton;
        int m_timerSeconds = 5;
        QTimer *m_timer;
    };

    class CloseConfirmationDialog : public QDialog {
        Q_OBJECT
    public:
        explicit CloseConfirmationDialog(bool autoCloseMode, QWidget *parent = nullptr);
        bool dontShowAgain() const;
    private slots: void updateTimer();
    private:
        QCheckBox *m_dontShowAgainCheckbox;
        QPushButton *m_okButton;
        int m_timerSeconds = 4;
        bool m_autoCloseMode;
        QTimer *m_timer;
    };

    class SlidingContainer : public QWidget {
        Q_OBJECT
    public:
        explicit SlidingContainer(QWidget* view1, QWidget* view2, QWidget *parent = nullptr);
        void slideToIndex(int index);
        bool isAnimating() const { return m_isAnimating; }
    signals:
        void animationFinished();
    protected:
        void resizeEvent(QResizeEvent *event) override;
        void paintEvent(QPaintEvent *event) override;
    private:
        QWidget *m_view1, *m_view2;
        int m_currentIndex = 0;
        QLabel *m_animLayer1, *m_animLayer2;
        bool m_isAnimating = false;
    };
}
#endif