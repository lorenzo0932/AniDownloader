#include "gui/Widgets.hpp"
#include <QApplication>
#include <QStyle>
#include <QPalette>
#include <QAbstractItemView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

namespace Gui {

ProgressBarTableWidgetItem::ProgressBarTableWidgetItem(const QString& text, int priority, int progress, bool isActive, const QString& phase)
    : QTableWidgetItem(text), m_priority(priority), m_progress(progress), m_isActive(isActive), m_phase(phase)
{
    setTextAlignment(Qt::AlignCenter);
}

QVariant ProgressBarTableWidgetItem::data(int role) const {
    if (role == Qt::DisplayRole) return QTableWidgetItem::data(role);
    if (role == Qt::UserRole) return m_priority;
    if (role == Qt::UserRole + 1) return m_progress;
    if (role == Qt::UserRole + 2) return m_isActive;
    if (role == Qt::UserRole + 3) return m_phase;
    return QTableWidgetItem::data(role);
}

void ProgressBarTableWidgetItem::setData(int role, const QVariant& value) {
    if (role == Qt::UserRole + 1) m_progress = value.toInt();
    else if (role == Qt::UserRole + 2) m_isActive = value.toBool();
    else if (role == Qt::UserRole + 3) m_phase = value.toString();
    QTableWidgetItem::setData(role, value);
}

bool ProgressBarTableWidgetItem::operator<(const QTableWidgetItem& other) const {
    const ProgressBarTableWidgetItem* otherPtr = dynamic_cast<const ProgressBarTableWidgetItem*>(&other);
    if (otherPtr) {
        return m_priority < otherPtr->m_priority;
    }
    return QTableWidgetItem::operator<(other);
}

void ProgressBarDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    QTableWidgetItem *item = nullptr;
    const QAbstractItemView *view = qobject_cast<const QAbstractItemView*>(option.widget);
    if (view) {
        const QTableWidget *table = qobject_cast<const QTableWidget*>(view);
        if (table) {
            item = table->item(index.row(), index.column());
        }
    }

    ProgressBarTableWidgetItem *pbItem = dynamic_cast<ProgressBarTableWidgetItem*>(item);

    if (pbItem && pbItem->isActive()) {
        // 1. Prepara il disegno della cella (selezione, alternanza colori riga)
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        opt.text = ""; // Non vogliamo il testo di default
        QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter, option.widget);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        // 2. Configurazione colori basata sul tema (stesso stile Python)
        bool isDark = qApp->palette().color(QPalette::Window).lightness() < 128;
        QColor chunkColor = isDark ? QColor("#6200ea") : QColor("#b39ddb");
        QColor barBgColor = isDark ? QColor(45, 45, 45) : QColor("#eeeeee");
        QColor textColor = isDark ? QColor("#ffffff") : QColor("#212121");

        // 3. Coordinate della barra
        QRect barRect = option.rect.adjusted(6, 6, -6, -6);
        int progress = pbItem->progress();
        int chunkWidth = (barRect.width() * progress) / 100;

        // 4. Disegno Sfondo Barra (Arrotondato)
        painter->setPen(Qt::NoPen);
        painter->setBrush(barBgColor);
        painter->drawRoundedRect(barRect, 5, 5);

        // 5. Disegno Chunk Progresso (Arrotondato)
        if (chunkWidth > 0) {
            QRect chunkRect = barRect;
            chunkRect.setWidth(chunkWidth);
            painter->setBrush(chunkColor);
            painter->drawRoundedRect(chunkRect, 5, 5);
        }

        // 6. Disegno Testo (Fase e Percentuale)
        QString displayPhase = "Elaborazione";
        if (pbItem->phase() == "download") displayPhase = "Download";
        else if (pbItem->phase() == "conversion") displayPhase = "Conversione";
        QString textString = QString("%1 %2%").arg(displayPhase).arg(progress);

        painter->setPen(textColor);
        if (!isDark) {
            QFont font = painter->font();
            font.setBold(true);
            painter->setFont(font);
        }
        painter->drawText(option.rect, Qt::AlignCenter, textString);

        painter->restore();
    } else {
        QStyledItemDelegate::paint(painter, option, index);
    }
}

StopConfirmationDialog::StopConfirmationDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Conferma Interruzione");
    setModal(true);
    setMinimumWidth(450);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(15);

    QLabel *messageLabel = new QLabel(
        "<b>ATTENZIONE: Stai per interrompere il processo.</b><br><br>"
        "Questa operazione terminerà forzatamente tutti i download e le conversioni in corso.<br>"
        "Tutti i file parziali e temporanei verranno eliminati.<br><br>"
        "Sei sicuro di voler procedere?", this);
    messageLabel->setWordWrap(true);
    layout->addWidget(messageLabel);

    m_dontShowAgainCheckbox = new QCheckBox("Non mostrare più questo avviso", this);
    m_dontShowAgainCheckbox->setCursor(Qt::PointingHandCursor);
    layout->addWidget(m_dontShowAgainCheckbox);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    m_okButton = new QPushButton(QString("Attendi %1s...").arg(m_timerSeconds), this);
    m_okButton->setObjectName("dangerButton");
    m_okButton->setEnabled(false);
    connect(m_okButton, &QPushButton::clicked, this, &QDialog::accept);

    QPushButton *cancelButton = new QPushButton("Annulla", this);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    buttonLayout->addStretch();
    buttonLayout->addWidget(cancelButton);
    buttonLayout->addWidget(m_okButton);
    layout->addLayout(buttonLayout);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &StopConfirmationDialog::updateTimer);
    m_timer->start(1000);
}

void StopConfirmationDialog::updateTimer() {
    m_timerSeconds--;
    if (m_timerSeconds > 0) {
        m_okButton->setText(QString("Attendi %1s...").arg(m_timerSeconds));
    } else {
        m_timer->stop();
        m_okButton->setText("OK, Interrompi");
        m_okButton->setEnabled(true);
    }
}

bool StopConfirmationDialog::dontShowAgain() const {
    return m_dontShowAgainCheckbox->isChecked();
}

CloseConfirmationDialog::CloseConfirmationDialog(bool autoCloseMode, QWidget *parent)
    : QDialog(parent), m_autoCloseMode(autoCloseMode)
{
    setWindowTitle("Chiusura Applicazione");
    setModal(true);
    setMinimumWidth(450);

    if (m_autoCloseMode) {
        setWindowFlags(windowFlags() | Qt::CustomizeWindowHint);
        setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);
    }

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(15);

    QLabel *messageLabel = new QLabel(
        "<b>ATTENZIONE: Download in corso.</b><br><br>"
        "Tutti i file convertiti e scaricati che non sono stati completati verranno distrutti.<br><br>"
        + QString(m_autoCloseMode ? "Chiusura automatica in corso..." : "Sei sicuro di voler uscire?"), this);
    messageLabel->setWordWrap(true);
    layout->addWidget(messageLabel);

    if (!m_autoCloseMode) {
        m_dontShowAgainCheckbox = new QCheckBox("Non mostrare più questo avviso", this);
        m_dontShowAgainCheckbox->setCursor(Qt::PointingHandCursor);
        layout->addWidget(m_dontShowAgainCheckbox);
    } else {
        m_dontShowAgainCheckbox = nullptr;
    }

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    m_okButton = new QPushButton(QString(m_autoCloseMode ? "Uscita tra %1s..." : "Attendi %1s...").arg(m_timerSeconds), this);
    m_okButton->setObjectName("dangerButton");
    m_okButton->setEnabled(false);
    connect(m_okButton, &QPushButton::clicked, this, &QDialog::accept);

    if (!m_autoCloseMode) {
        QPushButton *cancelButton = new QPushButton("Annulla", this);
        connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
        buttonLayout->addStretch();
        buttonLayout->addWidget(cancelButton);
    } else {
        buttonLayout->addStretch();
    }

    buttonLayout->addWidget(m_okButton);
    layout->addLayout(buttonLayout);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &CloseConfirmationDialog::updateTimer);
    m_timer->start(1000);
}

void CloseConfirmationDialog::updateTimer() {
    m_timerSeconds--;
    if (m_timerSeconds > 0) {
        m_okButton->setText(QString(m_autoCloseMode ? "Uscita tra %1s..." : "Attendi %1s...").arg(m_timerSeconds));
    } else {
        m_timer->stop();
        if (m_autoCloseMode) {
            accept();
        } else {
            m_okButton->setText("OK, Esci");
            m_okButton->setEnabled(true);
        }
    }
}

bool CloseConfirmationDialog::dontShowAgain() const {
    return m_dontShowAgainCheckbox ? m_dontShowAgainCheckbox->isChecked() : false;
}

} // namespace Gui