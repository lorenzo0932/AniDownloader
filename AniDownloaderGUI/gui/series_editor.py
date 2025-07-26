import os
from PyQt6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QLabel, QLineEdit,
    QPushButton, QFormLayout, QCheckBox, QSpinBox, QMessageBox,
    QFileDialog, QWidget
)
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QPixmap

class SeriesEditorDialog(QDialog):
    def __init__(self, series_data, is_new=False, parent=None):
        super().__init__(parent)
        self._is_new = is_new
        title = "Aggiungi Nuova Serie" if is_new else f"Modifica: {series_data.get('name', 'N/A')}"
        self.setWindowTitle(title)
        self.setMinimumSize(500, 550)
        
        self._series_data = series_data.copy()
        self._result_data = None
        self._is_deleted = False

        self._init_ui()
        self._populate_fields()

    def _init_ui(self):
        main_layout = QVBoxLayout(self)
        
        self._image_label = QLabel("Locandina non trovata")
        self._image_label.setMinimumHeight(300)
        self._image_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._image_label.setStyleSheet("border: 1px solid #ccc; background-color: #f0f0f0;")
        main_layout.addWidget(self._image_label)

        form_widget = QWidget()
        input_group_layout = QFormLayout(form_widget)
        self._name_input = QLineEdit()
        self._path_input = QLineEdit()
        path_layout = QHBoxLayout()
        path_layout.addWidget(self._path_input)
        path_browse_button = QPushButton("Sfoglia...")
        path_browse_button.clicked.connect(self._browse_series_path)
        path_layout.addWidget(path_browse_button)
        self._link_pattern_input = QLineEdit()
        self._continue_checkbox = QCheckBox()
        self._passed_episodes_input = QSpinBox()
        self._passed_episodes_input.setMinimum(0)
        self._passed_episodes_input.setMaximum(999)
        input_group_layout.addRow("Nome:", self._name_input)
        input_group_layout.addRow("Percorso:", path_layout)
        input_group_layout.addRow("Pattern Link:", self._link_pattern_input)
        input_group_layout.addRow("Continua:", self._continue_checkbox)
        input_group_layout.addRow("Ep. Passati:", self._passed_episodes_input)
        main_layout.addWidget(form_widget)
        
        main_layout.addStretch()

        button_layout = QHBoxLayout()
        self._delete_button = QPushButton("Elimina Serie");
        self._delete_button.setStyleSheet("background-color: #f44336; color: white; font-weight: bold;")
        self._delete_button.clicked.connect(self._delete_series)
        if self._is_new:
            self._delete_button.hide()
        
        save_button = QPushButton("Salva Modifiche");
        save_button.setDefault(True)
        save_button.clicked.connect(self._save_changes)

        cancel_button = QPushButton("Annulla");
        cancel_button.clicked.connect(self.reject)
        
        button_layout.addWidget(self._delete_button)
        button_layout.addStretch(1)
        button_layout.addWidget(cancel_button)
        button_layout.addWidget(save_button)
        main_layout.addLayout(button_layout)

    def _populate_fields(self):
        self._name_input.setText(self._series_data.get("name", ""))
        self._path_input.setText(self._series_data.get("path", ""))
        self._link_pattern_input.setText(self._series_data.get("link_pattern", ""))
        self._continue_checkbox.setChecked(self._series_data.get("continue", False))
        self._passed_episodes_input.setValue(self._series_data.get("passed_episodes", 0))
        self._load_poster()

    def _load_poster(self):
        path = self._path_input.text()
        if path and os.path.exists(os.path.dirname(path)):
            # **RIPRISTINATO:** Logica per la locandina corretta
            image_path = os.path.join(os.path.dirname(path), "folder.jpg")
            if os.path.exists(image_path) and (pixmap := QPixmap(image_path)) and not pixmap.isNull():
                self._image_label.setPixmap(pixmap.scaled(self._image_label.size(), Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation))
            else:
                self._image_label.setText("folder.jpg non trovato")
        else:
            self._image_label.setText("Specificare un percorso valido")
            
    def _browse_series_path(self):
        start_dir = self._path_input.text() if os.path.isdir(self._path_input.text()) else ""
        selected_dir = QFileDialog.getExistingDirectory(self, "Seleziona Cartella Serie", start_dir)
        if selected_dir:
            # Aggiunge un file fittizio al percorso per coerenza con la logica `dirname`
            self._path_input.setText(os.path.join(selected_dir, "dummyfile.mkv"))
            self._load_poster()

    def _save_changes(self):
        confirm_text = "Sei sicuro di voler aggiungere questa nuova serie?" if self._is_new else "Sei sicuro di voler salvare le modifiche a questa serie?"
        reply = QMessageBox.question(self, "Conferma", confirm_text,
                                     QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                                     QMessageBox.StandardButton.No)
        if reply == QMessageBox.StandardButton.Yes:
            self._result_data = {
                "name": self._name_input.text().strip(),
                "path": self._path_input.text().strip(),
                "link_pattern": self._link_pattern_input.text().strip()
            }
            if self._continue_checkbox.isChecked():
                self._result_data["continue"] = True
                self._result_data["passed_episodes"] = self._passed_episodes_input.value()
            
            self.accept()

    def _delete_series(self):
        reply = QMessageBox.question(self, "Conferma Eliminazione", "Sei sicuro di voler eliminare definitivamente questa serie?",
                                     QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                                     QMessageBox.StandardButton.No)
        if reply == QMessageBox.StandardButton.Yes:
            self._is_deleted = True
            self.accept()
            
    def get_data(self):
        return self._is_deleted, self._result_data
