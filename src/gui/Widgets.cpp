#include "gui/Widgets.hpp"
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>

namespace Gui {

ProgressBarTableWidgetItem::ProgressBarTableWidgetItem(const QString& t, int p, int pr, bool a, const QString& ph)
    : QTableWidgetItem(t), m_priority(p), m_progress(pr), m_isActive(a), m_phase(ph) {
    setTextAlignment(Qt::AlignCenter);
}

QVariant ProgressBarTableWidgetItem::data(int r) const {
    if (r == Qt::UserRole + 1) return m_progress;
    if (r == Qt::UserRole + 2) return m_isActive;
    if (r == Qt::UserRole + 3) return m_phase;
    return QTableWidgetItem::data(r);
}

void ProgressBarTableWidgetItem::setData(int r, const QVariant& v) {
    if (r == Qt::UserRole + 1) {
        m_progress = v.toInt();
    } else if (r == Qt::UserRole + 2) {
        m_isActive = v.toBool();
    } else if (r == Qt::UserRole + 3) {
        m_phase = v.toString();
    } else {
        QTableWidgetItem::setData(r, v);
    }
}

// LOGICA ORDINAMENTO: Priority 0 (Download) va in alto, Priority 3 (Skip) va in basso
bool ProgressBarTableWidgetItem::operator<(const QTableWidgetItem& other) const {
    const auto* o = dynamic_cast<const ProgressBarTableWidgetItem*>(&other);
    if (o) return this->m_priority < o->m_priority;
    return QTableWidgetItem::operator<(other);
}

void ProgressBarDelegate::paint(QPainter *p, const QStyleOptionViewItem &o, const QModelIndex &i) const {
    auto *table = qobject_cast<const QTableWidget*>(o.widget);
    if (!table) return;

    auto *item = dynamic_cast<ProgressBarTableWidgetItem*>(table->item(i.row(), i.column()));
    if (item && item->isActive()) {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);

        bool isDark = qApp->palette().color(QPalette::Window).lightness() < 128;
        QRect r = o.rect.adjusted(6, 6, -6, -6);

        p->setPen(Qt::NoPen);
        p->setBrush(isDark ? QColor(45, 45, 45) : QColor(230, 230, 230));
        p->drawRoundedRect(r, 5, 5);

        int w = r.width() * item->progress() / 100;
        if (w > 0) {
            QRect pr = r;
            pr.setWidth(w);
            p->setBrush(QColor("#6200ea"));
            p->drawRoundedRect(pr, 5, 5);
        }

        p->setPen(isDark ? Qt::white : Qt::black);
        p->drawText(o.rect, Qt::AlignCenter, QString("%1%").arg(item->progress()));
        p->restore();
    } else {
        QStyledItemDelegate::paint(p, o, i);
    }
}

StopConfirmationDialog::StopConfirmationDialog(QWidget *p) : QDialog(p) {
    setWindowTitle("Conferma");
    setModal(true);
    setMinimumWidth(400);

    auto *l = new QVBoxLayout(this);
    l->addWidget(new QLabel("Vuoi interrompere il download?"));

    m_dontShowAgainCheckbox = new QCheckBox("Non mostrare più");
    l->addWidget(m_dontShowAgainCheckbox);

    m_okButton = new QPushButton("Attendi...");
    m_okButton->setEnabled(false);
    m_okButton->setObjectName("dangerButton");

    auto *bl = new QHBoxLayout();
    auto *c = new QPushButton("Annulla");
    connect(c, &QPushButton::clicked, this, &QDialog::reject);

    bl->addStretch();
    bl->addWidget(c);
    bl->addWidget(m_okButton);
    l->addLayout(bl);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &StopConfirmationDialog::updateTimer);
    m_timer->start(1000);

    connect(m_okButton, &QPushButton::clicked, this, &QDialog::accept);
}

void StopConfirmationDialog::updateTimer() {
    if (--m_timerSeconds <= 0) {
        m_timer->stop();
        m_okButton->setText("OK");
        m_okButton->setEnabled(true);
    } else {
        m_okButton->setText(QString("Attendi %1s...").arg(m_timerSeconds));
    }
}

bool StopConfirmationDialog::dontShowAgain() const {
    return m_dontShowAgainCheckbox->isChecked();
}

CloseConfirmationDialog::CloseConfirmationDialog(bool a, QWidget *p) : QDialog(p), m_autoCloseMode(a) {
    setWindowTitle("Chiusura");
    setModal(true);
    auto *l = new QVBoxLayout(this);
    l->addWidget(new QLabel("Download in corso. Uscire comunque?"));

    if (!a) {
        m_dontShowAgainCheckbox = new QCheckBox("Non mostrare più");
        l->addWidget(m_dontShowAgainCheckbox);
    }

    m_okButton = new QPushButton("Attendi...");
    m_okButton->setEnabled(false);
    m_okButton->setObjectName("dangerButton");

    auto *bl = new QHBoxLayout();
    connect(m_okButton, &QPushButton::clicked, this, &QDialog::accept);

    if (!a) {
        auto *c = new QPushButton("Annulla");
        connect(c, &QPushButton::clicked, this, &QDialog::reject);
        bl->addStretch();
        bl->addWidget(c);
    } else {
        bl->addStretch();
    }

    bl->addWidget(m_okButton);
    l->addLayout(bl);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &CloseConfirmationDialog::updateTimer);
    m_timer->start(1000);
}

void CloseConfirmationDialog::updateTimer() {
    if (--m_timerSeconds <= 0) {
        m_timer->stop();
        if (m_autoCloseMode) {
            accept();
        } else {
            m_okButton->setText("OK");
            m_okButton->setEnabled(true);
        }
    } else {
        m_okButton->setText(QString("Attendi %1s...").arg(m_timerSeconds));
    }
}

bool CloseConfirmationDialog::dontShowAgain() const {
    return m_dontShowAgainCheckbox ? m_dontShowAgainCheckbox->isChecked() : false;
}

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