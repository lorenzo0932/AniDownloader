from PyQt6.QtWidgets import (
    QTableWidgetItem, QDialog, QVBoxLayout, QLabel,
    QCheckBox, QHBoxLayout, QPushButton
)
from PyQt6.QtCore import QTimer

class StatusTableWidgetItem(QTableWidgetItem):
    def __init__(self, text, priority):
        super().__init__(text)
        self.priority = priority

    def __lt__(self, other):
        if isinstance(other, StatusTableWidgetItem):
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
