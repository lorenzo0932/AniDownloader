from PyQt6.QtWidgets import (
    QTableWidgetItem, QDialog, QVBoxLayout, QLabel,
    QCheckBox, QHBoxLayout, QPushButton, QStyledItemDelegate, 
    QStyleOptionProgressBar, QStyle, QApplication, QStyleOptionViewItem
)
from PyQt6.QtCore import QTimer, Qt
from PyQt6.QtGui import QColor, QPalette

# --- CLASSI PROGRESS BAR ---

class ProgressBarTableWidgetItem(QTableWidgetItem):
    def __init__(self, text, priority, progress=0, is_active=False, phase=""):
        super().__init__(text)
        self.priority = priority
        self._progress = progress
        self._is_active = is_active
        self._phase = phase
        self.setTextAlignment(Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignHCenter)

    def data(self, role):
        if role == Qt.ItemDataRole.DisplayRole:
            return super().data(role)
        if role == Qt.ItemDataRole.UserRole:
            return self.priority
        if role == Qt.ItemDataRole.UserRole + 1:
            return self._progress
        if role == Qt.ItemDataRole.UserRole + 2:
            return self._is_active
        if role == Qt.ItemDataRole.UserRole + 3:
            return self._phase
        return super().data(role)

    def setData(self, role, value):
        if role == Qt.ItemDataRole.UserRole + 1:
            self._progress = value
        elif role == Qt.ItemDataRole.UserRole + 2:
            self._is_active = value
        elif role == Qt.ItemDataRole.UserRole + 3:
            self._phase = value
        super().setData(role, value)

    def __lt__(self, other):
        if hasattr(other, 'priority'):
            return self.priority < other.priority
        return super().__lt__(other)

    def progress(self):
        return self._progress

    def is_active(self):
        return self._is_active

    def phase(self):
        return self._phase

class ProgressBarDelegate(QStyledItemDelegate):
    def paint(self, painter, option, index):
        item = None
        if hasattr(option.widget, 'item'):
            item = option.widget.item(index.row(), index.column())
        
        if isinstance(item, ProgressBarTableWidgetItem) and item.is_active():
            # 1. DISEGNO SFONDO CELLA (Senza Testo)
            opt = QStyleOptionViewItem(option)
            self.initStyleOption(opt, index)
            opt.text = "" 
            style = option.widget.style()
            style.drawControl(QStyle.ControlElement.CE_ItemViewItem, opt, painter, option.widget)

            # 2. CONFIGURAZIONE COLORI IN BASE AL TEMA
            main_window = option.widget.window()
            is_dark = True
            if main_window and hasattr(main_window, '_is_dark_theme'):
                is_dark = getattr(main_window, '_is_dark_theme')

            if is_dark:
                # Tema Scuro: Viola intenso, sfondo scuro, testo bianco
                chunk_color = QColor("#6200ea") 
                bar_bg_color = QColor(45, 45, 45) # Grigio molto scuro
                text_color = QColor("#ffffff")
            else:
                # Tema Chiaro: Viola pastello, sfondo grigio chiaro, testo nero
                # Usiamo un viola più chiaro (#b39ddb) per far risaltare il testo nero
                chunk_color = QColor("#b39ddb") 
                bar_bg_color = QColor("#eeeeee") # Grigio chiaro per visibilità
                text_color = QColor("#212121")

            # 3. DISEGNO PROGRESS BAR (Grafica)
            bar_option = QStyleOptionProgressBar()
            bar_option.rect = option.rect.adjusted(4, 4, -4, -4)
            bar_option.minimum = 0
            bar_option.maximum = 100
            bar_option.progress = item.progress()
            bar_option.textVisible = False # Testo gestito manualmente per precisione
            
            # Applichiamo i colori alla palette della barra
            palette = option.palette
            palette.setColor(QPalette.ColorRole.Highlight, chunk_color) # Colore caricamento
            palette.setColor(QPalette.ColorRole.Base, bar_bg_color)      # Colore sfondo barra
            bar_option.palette = palette

            style.drawControl(QStyle.ControlElement.CE_ProgressBar, bar_option, painter, option.widget)

            # 4. DISEGNO TESTO MANUALE
            progress = item.progress()
            phase = item.data(Qt.ItemDataRole.UserRole + 3)
            display_phase = "Download" if phase == "download" else "Conversione" if phase == "conversion" else "Elaborazione"
            
            text_string = f"{display_phase} {progress}%"

            painter.save()
            painter.setPen(text_color)
            # Usiamo un font leggermente più grassetto per la barra se in tema chiaro
            if not is_dark:
                font = painter.font()
                font.setBold(True)
                painter.setFont(font)
                
            painter.drawText(option.rect, Qt.AlignmentFlag.AlignCenter, text_string)
            painter.restore()
            
        else:
            # Stati non attivi (In coda, Fatto, ecc.)
            super().paint(painter, option, index)

# --- CLASSI DIALOGHI E STATUS ---

class StatusTableWidgetItem(QTableWidgetItem):
    def __init__(self, text, priority):
        super().__init__(text)
        self.priority = priority

    def __lt__(self, other):
        if isinstance(other, StatusTableWidgetItem):
            return self.priority < other.priority
        if hasattr(other, 'priority'):
            return self.priority < other.priority
        return super().__lt__(other)

class StopConfirmationDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Conferma Interruzione")
        self.setModal(True)
        self.setMinimumWidth(400)

        _layout = QVBoxLayout(self)

        _message_label = QLabel(
            "<b>ATTENZIONE: Stai per interrompere il processo.</b><br><br>"
            "Questa operazione terminerà forzatamente tutti i download e le conversioni in corso.<br>"
            "Tutti i file parziali e temporanei verranno eliminati.<br><br>"
            "Sei sicuro di voler procedere?"
        )
        _message_label.setWordWrap(True)
        _layout.addWidget(_message_label)

        self._dont_show_again_checkbox = QCheckBox("Non mostrare più questo avviso")
        _layout.addWidget(self._dont_show_again_checkbox)

        _button_layout = QHBoxLayout()
        self._ok_button = QPushButton("Attendi 5s...")
        self._ok_button.setEnabled(False)
        self._ok_button.clicked.connect(self.accept)

        _cancel_button = QPushButton("Annulla")
        _cancel_button.clicked.connect(self.reject)

        _button_layout.addStretch()
        _button_layout.addWidget(_cancel_button)
        _button_layout.addWidget(self._ok_button)
        _layout.addLayout(_button_layout)

        self._timer_seconds = 5
        self._timer = QTimer(self)
        self._timer.timeout.connect(self.update_timer)
        self._timer.start(1000)

    def update_timer(self):
        self._timer_seconds -= 1
        if self._timer_seconds > 0:
            self._ok_button.setText(f"Attendi {self._timer_seconds}s...")
        else:
            self._timer.stop()
            self._ok_button.setText("OK, Interrompi")
            self._ok_button.setEnabled(True)

    def dont_show_again(self):
        return self._dont_show_again_checkbox.isChecked()