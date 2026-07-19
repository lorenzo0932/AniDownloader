#include "gui/SlidingContainer.hpp"
#include <QPainter>
#include <QApplication>

namespace Gui {

SlidingContainer::SlidingContainer(QWidget* v1, QWidget* v2, QWidget *parent)
    : QWidget(parent), m_view1(v1), m_view2(v2) {
    setObjectName("slidingContainer");
    setAutoFillBackground(true);

    m_view1->setParent(this);
    m_view2->setParent(this);

    m_animLayer1 = new QLabel(this);
    m_animLayer2 = new QLabel(this);

    m_animLayer1->hide();
    m_animLayer2->hide();

    m_view1->show();
    m_view2->hide();
}

void SlidingContainer::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), palette().window());
}

void SlidingContainer::slideToIndex(int index) {
    if (index == m_currentIndex || m_isAnimating) return;
    m_isAnimating = true;
    int w = width(), h = height();

    QPixmap p_curr(w, h);
    p_curr.fill(palette().window().color());
    if (m_currentIndex == 0) m_view1->render(&p_curr);
    else m_view2->render(&p_curr);

    QWidget *target = (index == 0) ? m_view1 : m_view2;
    target->show();
    target->setGeometry(0, 0, w, h);
    target->repaint();
    QApplication::processEvents();

    QPixmap p_targ(w, h);
    p_targ.fill(palette().window().color());
    target->render(&p_targ);

    m_view1->hide();
    m_view2->hide();

    m_animLayer1->setPixmap(p_curr);
    m_animLayer2->setPixmap(p_targ);

    m_animLayer1->setGeometry(0, 0, w, h);
    m_animLayer2->setGeometry(index == 1 ? w : -w, 0, w, h);

    m_animLayer1->show();
    m_animLayer2->show();

    auto *a1 = new QPropertyAnimation(m_animLayer1, "pos");
    auto *a2 = new QPropertyAnimation(m_animLayer2, "pos");

    a1->setDuration(450);
    a2->setDuration(450);
    a1->setEasingCurve(QEasingCurve::OutQuint);
    a2->setEasingCurve(QEasingCurve::OutQuint);

    a1->setEndValue(QPoint(index == 1 ? -w : w, 0));
    a2->setEndValue(QPoint(0, 0));

    auto *g = new QParallelAnimationGroup(this);
    g->addAnimation(a1);
    g->addAnimation(a2);

    connect(g, &QParallelAnimationGroup::finished, this, [=]() {
        m_animLayer1->hide();
        m_animLayer2->hide();
        if (index == 0) m_view1->show();
        else m_view2->show();
        m_currentIndex = index;
        m_isAnimating = false;
        emit animationFinished();
        g->deleteLater();
    });

    g->start();
}

void SlidingContainer::resizeEvent(QResizeEvent *) {
    if (!m_isAnimating) {
        m_view1->setGeometry(0, 0, width(), height());
        m_view2->setGeometry(0, 0, width(), height());
    }
}

} // namespace Gui
