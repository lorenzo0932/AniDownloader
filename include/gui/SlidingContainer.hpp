#pragma once

#include <QWidget>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QEasingCurve>
#include <QResizeEvent>

namespace Gui {

class SlidingContainer : public QWidget {
    Q_OBJECT
public:
    explicit SlidingContainer(QWidget* view1, QWidget* view2, QWidget *parent = nullptr) 
        : QWidget(parent), m_view1(view1), m_view2(view2), m_currentIndex(0) 
    {
        // Imposta questo container come genitore delle due viste
        m_view1->setParent(this);
        m_view2->setParent(this);
        
        // Inizialmente la vista 1 (Download) è visibile, la 2 (Manager) è nascosta
        m_view1->show();
        m_view2->hide(); 
    }

    void slideToIndex(int index) {
        if (index == m_currentIndex) return;

        int w = this->width();
        int h = this->height();

        // Rende visibili entrambe le schermate durante l'animazione
        m_view1->show();
        m_view2->show();

        QPropertyAnimation *anim1 = new QPropertyAnimation(m_view1, "pos");
        QPropertyAnimation *anim2 = new QPropertyAnimation(m_view2, "pos");

        anim1->setDuration(300); // 300ms per un'animazione fluida ma veloce
        anim2->setDuration(300);
        
        // Effetto di decellerazione morbida alla fine del movimento
        anim1->setEasingCurve(QEasingCurve::OutCubic);
        anim2->setEasingCurve(QEasingCurve::OutCubic);

        if (index == 1) { 
            // Scivola verso il Manager (la vista 2 entra da DESTRA)
            m_view2->setGeometry(w, 0, w, h);
            anim1->setEndValue(QPoint(-w, 0));
            anim2->setEndValue(QPoint(0, 0));
        } else { 
            // Scivola verso i Download (la vista 1 entra da SINISTRA)
            m_view1->setGeometry(-w, 0, w, h);
            anim2->setEndValue(QPoint(w, 0));
            anim1->setEndValue(QPoint(0, 0));
        }

        QParallelAnimationGroup *group = new QParallelAnimationGroup(this);
        group->addAnimation(anim1);
        group->addAnimation(anim2);
        
        connect(group, &QParallelAnimationGroup::finished, this, [this, index, group]() {
            // Nasconde la vista che è uscita dallo schermo per risparmiare risorse
            if (index == 0) m_view2->hide();
            else m_view1->hide();
            
            m_currentIndex = index;
            group->deleteLater();
        });

        group->start();
    }

protected:
    void resizeEvent(QResizeEvent *event) override {
        // Mantiene corrette le proporzioni se l'utente ridimensiona la finestra
        int w = event->size().width();
        int h = event->size().height();
        
        if (m_currentIndex == 0) {
            m_view1->setGeometry(0, 0, w, h);
            m_view2->setGeometry(w, 0, w, h);
        } else {
            m_view1->setGeometry(-w, 0, w, h);
            m_view2->setGeometry(0, 0, w, h);
        }
        QWidget::resizeEvent(event);
    }

private:
    QWidget *m_view1;
    QWidget *m_view2;
    int m_currentIndex;
};

} // namespace Gui