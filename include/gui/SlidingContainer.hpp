#pragma once

#include <QWidget>
#include <QLabel>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QEasingCurve>
#include <QPaintEvent>

namespace Gui {

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

} // namespace Gui
