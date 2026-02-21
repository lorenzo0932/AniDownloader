import os
from PyQt6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QLabel, QLineEdit,
    QPushButton, QFormLayout, QCheckBox, QSpinBox, QMessageBox,
    QFileDialog, QWidget, QRadioButton, QGroupBox, QButtonGroup
)
from PyQt6.QtCore import Qt
from utils.image_loader import load_poster_image

class SeriesEditorDialog(QDialog):
    def __init__(self, series_data, is_new=False, parent=None):
        super().__init__(parent)
        self._is_new = is_new
        title = "Aggiungi Nuova Serie" if is_new else f"Modifica: {series_data.get('name', 'N/A')}"
        self.setWindowTitle(title)
        self.setMinimumSize(500, 600)
        
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
        form_layout = QFormLayout(form_widget)

        service_groupbox = QGroupBox("Servizio di Download")
        service_layout = QHBoxLayout()
        self._rb_animeW = QRadioButton("AnimeW Scraper")
        self._rb_animeU = QRadioButton("AnimeU Scraper")
        self._service_button_group = QButtonGroup(self)
        self._service_button_group.addButton(self._rb_animeW, 1) 
        self._service_button_group.addButton(self._rb_animeU, 2)
        self._service_button_group.setExclusive(True)
        service_layout.addWidget(self._rb_animeW)
        service_layout.addWidget(self._rb_animeU)
        service_groupbox.setLayout(service_layout)
        form_layout.addRow(service_groupbox)
        
        self._name_input = QLineEdit()
        
        # --- MODIFICA CHIAVE INIZIA ---
        # 1. Creiamo il layout orizzontale come prima
        path_layout = QHBoxLayout()
        self._path_input = QLineEdit()
        path_browse_button = QPushButton("Sfoglia...")
        path_browse_button.clicked.connect(self._browse_series_path)
        path_layout.addWidget(self._path_input)
        path_layout.addWidget(path_browse_button)
        
        # 2. Creiamo un widget contenitore VUOTO
        path_container_widget = QWidget()
        
        # 3. Applichiamo il layout orizzontale al widget contenitore
        path_container_widget.setLayout(path_layout)
        
        # 4. Rimuoviamo i margini dal layout interno per un aspetto pulito
        path_layout.setContentsMargins(0, 0, 0, 0)
        
        # 5. Aggiungiamo il WIDGET contenitore al QFormLayout, non il layout
        form_layout.addRow("Nome:", self._name_input)
        form_layout.addRow("Percorso Cartella:", path_container_widget)
        # --- FINE MODIFICA ---
        
        self._series_page_url_input = QLineEdit()
        self._filename_root_input = QLineEdit()
        self._filename_root_input.setPlaceholderText("Es: Dandadan (per nomi file coerenti)")
        
        self._continue_checkbox = QCheckBox()
        self._passed_episodes_input = QSpinBox()
        self._passed_episodes_input.setMinimum(0)
        self._passed_episodes_input.setMaximum(999)

        form_layout.addRow("URL Pagina Serie:", self._series_page_url_input)
        form_layout.addRow("Radice Nome File (Opzionale):", self._filename_root_input)
        
        continue_layout = QHBoxLayout()
        continue_layout.addWidget(self._continue_checkbox)
        continue_layout.addStretch()
        form_layout.addRow("Continua numerazione:", continue_layout)
        form_layout.addRow("Episodi Passati:", self._passed_episodes_input)

        main_layout.addWidget(form_widget)
        main_layout.addStretch()

        button_layout = QHBoxLayout()
        self._delete_button = QPushButton("Elimina Serie")
        self._delete_button.setStyleSheet("background-color: #f44336; color: white; font-weight: bold;")
        self._delete_button.clicked.connect(self._delete_series)
        if self._is_new: self._delete_button.hide()
        
        cancel_button = QPushButton("Annulla"); cancel_button.clicked.connect(self.reject)
        save_button = QPushButton("Salva Modifiche"); save_button.setDefault(True); save_button.clicked.connect(self._save_changes)
        
        button_layout.addWidget(self._delete_button); button_layout.addStretch(1)
        button_layout.addWidget(cancel_button); button_layout.addWidget(save_button)
        main_layout.addLayout(button_layout)

    def _populate_fields(self):
        self._name_input.setText(self._series_data.get("name", ""))
        self._path_input.setText(self._series_data.get("path", ""))
        self._series_page_url_input.setText(self._series_data.get("series_page_url", ""))
        self._filename_root_input.setText(self._series_data.get("filename_root", ""))
        self._continue_checkbox.setChecked(self._series_data.get("continue", False))
        self._passed_episodes_input.setValue(self._series_data.get("passed_episodes", 0))
        
        service = self._series_data.get("service")
        if service == "animeW_scraper": self._rb_animeW.setChecked(True)
        elif service == "animeU_scraper": self._rb_animeU.setChecked(True)
        else: self._rb_animeW.setChecked(True)
        
        self._load_poster()

    def _load_poster(self):
        path = self._path_input.text()
        if path and os.path.isdir(path):
            load_poster_image(self._image_label, path)
        else:
            self._image_label.setText("Percorso non valido o locandina non trovata")
            
    def _browse_series_path(self):
        start_dir = self._path_input.text() if os.path.isdir(self._path_input.text()) else ""
        selected_dir = QFileDialog.getExistingDirectory(self, "Seleziona Cartella Serie", start_dir)
        if selected_dir:
            self._path_input.setText(selected_dir)
            self._load_poster()

    def _save_changes(self):
        if not all([self._name_input.text().strip(), self._path_input.text().strip(), self._series_page_url_input.text().strip()]):
            QMessageBox.warning(self, "Campi Mancanti", "I campi 'Nome', 'Percorso' e 'URL Pagina Serie' sono obbligatori.")
            return

        confirm_text = "Aggiungere questa nuova serie?" if self._is_new else "Salvare le modifiche a questa serie?"
        reply = QMessageBox.question(self, "Conferma", confirm_text, QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No, QMessageBox.StandardButton.No)
        
        if reply == QMessageBox.StandardButton.Yes:
            self._result_data = {
                "name": self._name_input.text().strip(),
                "path": self._path_input.text().strip(),
                "series_page_url": self._series_page_url_input.text().strip()
            }
            
            checked_id = self._service_button_group.checkedId()
            if checked_id == 1: self._result_data["service"] = "animeW_scraper"
            elif checked_id == 2: self._result_data["service"] = "animeU_scraper"

            filename_root = self._filename_root_input.text().strip()
            if filename_root: self._result_data["filename_root"] = filename_root
            
            if self._continue_checkbox.isChecked():
                self._result_data["continue"] = True
                self._result_data["passed_episodes"] = self._passed_episodes_input.value()
            else:
                self._result_data["continue"] = False
                self._result_data["passed_episodes"] = 0
            
            super().accept()

    def _delete_series(self):
        reply = QMessageBox.question(self, "Conferma Eliminazione", "Sei sicuro di voler eliminare definitivamente questa serie?", QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No, QMessageBox.StandardButton.No)
        if reply == QMessageBox.StandardButton.Yes:
            self._is_deleted = True
            self.accept()
            
    def get_data(self):
        return self._is_deleted, self._result_data