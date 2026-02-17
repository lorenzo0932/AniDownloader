import os
from pathlib import Path
from PyQt6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QLabel,
    QCheckBox, QPushButton, QSpinBox, QGroupBox, 
    QFileDialog, QLineEdit, QMessageBox, QWidget, QScrollArea, QStyle
)
from PyQt6.QtCore import Qt, QSettings
from PyQt6.QtGui import QCursor

class SettingsDialog(QDialog):
    def __init__(self, config_manager, settings: QSettings, parent=None):
        super().__init__(parent)
        self.config_manager = config_manager
        self.q_settings = settings
        self.setWindowTitle("Impostazioni AniDownloader")
        self.setModal(True)
        self.resize(600, 500)
        self.setMinimumWidth(500)
        
        # Track initial paths to detect changes
        self.initial_json_path = self.config_manager.get("json_file_path")
        self.initial_output_dir = self.config_manager.get("output_dir")
        self.paths_changed = False

        # Main Layout container
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(0, 0, 0, 0)
        
        # Scroll Area to handle resizing and small screens
        scroll_area = QScrollArea()
        scroll_area.setWidgetResizable(True)
        scroll_area.setFrameShape(QScrollArea.Shape.NoFrame)
        
        # Content Widget inside Scroll Area
        content_widget = QWidget()
        self.content_layout = QVBoxLayout(content_widget)
        self.content_layout.setSpacing(20)
        self.content_layout.setContentsMargins(20, 20, 20, 20)
        
        # --- SEZIONI ---
        self._init_paths_section()
        self._init_video_section()
        self._init_app_section()
        
        # Push everything to the top
        self.content_layout.addStretch()
        
        scroll_area.setWidget(content_widget)
        main_layout.addWidget(scroll_area)

        # Buttons (Always visible at bottom)
        self._init_buttons(main_layout)

    def _init_paths_section(self):
        group = QGroupBox("Gestione File e Percorsi")
        layout = QVBoxLayout(group)
        layout.setSpacing(10)

        # JSON File
        json_lbl = QLabel("File Database Serie (JSON):")
        layout.addWidget(json_lbl)
        
        json_row = QHBoxLayout()
        self.json_path_edit = QLineEdit(str(self.config_manager.get("json_file_path")))
        self.json_path_edit.setReadOnly(True)
        
        json_btn = QPushButton("...")
        json_btn.setFixedWidth(40)
        json_btn.setToolTip("Sfoglia...")
        json_btn.clicked.connect(self._browse_json)
        
        json_row.addWidget(self.json_path_edit)
        json_row.addWidget(json_btn)
        layout.addLayout(json_row)

        # Output Dir
        out_lbl = QLabel("Cartella di Destinazione (Output):")
        layout.addWidget(out_lbl)
        
        out_row = QHBoxLayout()
        self.output_dir_edit = QLineEdit(str(self.config_manager.get("output_dir")))
        self.output_dir_edit.setReadOnly(True)
        
        out_btn = QPushButton("...")
        out_btn.setFixedWidth(40)
        out_btn.setToolTip("Sfoglia...")
        out_btn.clicked.connect(self._browse_output)
        
        out_row.addWidget(self.output_dir_edit)
        out_row.addWidget(out_btn)
        layout.addLayout(out_row)

        self.content_layout.addWidget(group)

    def _init_video_section(self):
        group = QGroupBox("Codifica e Prestazioni Video")
        layout = QVBoxLayout(group)
        layout.setSpacing(10) # Reduced spacing

        # H.265
        h265_layout = QHBoxLayout()
        self.h265_checkbox = QCheckBox("Abilita compressione H.265 (HEVC)")
        self.h265_checkbox.setChecked(self.config_manager.get("convert_to_h265", True))
        self.h265_checkbox.setCursor(Qt.CursorShape.PointingHandCursor)
        h265_layout.addWidget(self.h265_checkbox)
        layout.addLayout(h265_layout)
        
        h265_desc = QLabel(
            "Riduce le dimensioni del file (~50%) mantenendo la qualità."
        )
        h265_desc.setWordWrap(True)
        h265_desc.setStyleSheet("color: #b0b0b0; font-size: 9pt; margin-left: 24px;")
        layout.addWidget(h265_desc)

        # Separator line
        line = QWidget()
        line.setFixedHeight(1)
        line.setStyleSheet("background-color: #404040; margin-top: 5px; margin-bottom: 5px;")
        layout.addWidget(line)

        # Chunks
        chunk_title = QLabel("Elaborazione Parallela (Chunk Splitting)")
        chunk_title.setStyleSheet("font-weight: bold;")
        layout.addWidget(chunk_title)

        chunk_desc = QLabel(
            "Divide il video in segmenti per utilizzare più core della CPU contemporaneamente."
        )
        chunk_desc.setWordWrap(True)
        chunk_desc.setStyleSheet("color: #b0b0b0; font-size: 9pt;")
        layout.addWidget(chunk_desc)

        chunk_ctrl_layout = QHBoxLayout()
        chunk_ctrl_layout.setContentsMargins(0, 0, 0, 0)
        chunk_ctrl_layout.addWidget(QLabel("Numero di Chunk:"))
        self.chunk_spin = QSpinBox()
        self.chunk_spin.setRange(1, 32)
        self.chunk_spin.setValue(self.config_manager.get("num_chunks", 1))
        self.chunk_spin.setFixedWidth(80)
        chunk_ctrl_layout.addWidget(self.chunk_spin)
        chunk_ctrl_layout.addStretch()
        layout.addLayout(chunk_ctrl_layout)

        # Combined Warning Box
        warning_container = QWidget()
        warning_container.setObjectName("warningLabel") # Use ID for styling
        warning_layout = QHBoxLayout(warning_container)
        warning_layout.setContentsMargins(10, 10, 10, 10)
        warning_layout.setSpacing(15)

        # Icon
        warning_icon_lbl = QLabel()
        icon = self.style().standardIcon(QStyle.StandardPixmap.SP_MessageBoxWarning)
        warning_icon_lbl.setPixmap(icon.pixmap(32, 32))
        warning_layout.addWidget(warning_icon_lbl, 0, Qt.AlignmentFlag.AlignTop)

        # Text
        warning_text = QLabel(
            "<b>Attenzione alle Prestazioni:</b><br>"
            "• <b>H.265:</b> Richiede molta potenza di calcolo. La conversione può durare molto tempo e allunga parecchio il tempo di \"ready to use\" del video.<br>"
            "• <b>Chunk > 1:</b> Aumenta drasticamente l'uso di RAM e CPU per velocizzare il processo."
        )
        warning_text.setWordWrap(True)
        warning_text.setStyleSheet("background-color: transparent; border: none;")
        warning_layout.addWidget(warning_text, 1)

        layout.addWidget(warning_container)

        self.content_layout.addWidget(group)

    def _init_app_section(self):
        group = QGroupBox("Avvisi")
        layout = QVBoxLayout(group)

        # Toggle Confirmation
        self.confirm_stop_checkbox = QCheckBox("Mostra conferma interruzione download")
        self.confirm_stop_checkbox.setCursor(Qt.CursorShape.PointingHandCursor)
        
        # Load current state (Default is True)
        current_state = self.q_settings.value("show_stop_warning", True, type=bool)
        self.confirm_stop_checkbox.setChecked(current_state)
        
        layout.addWidget(self.confirm_stop_checkbox)

        self.content_layout.addWidget(group)

    def _init_buttons(self, main_layout):
        container = QWidget()
        container.setObjectName("settingsFooter") # Use ID for styling
        
        btn_layout = QHBoxLayout(container)
        btn_layout.setContentsMargins(20, 15, 20, 15)
        btn_layout.setSpacing(10)
        
        save_btn = QPushButton("Salva Modifiche")
        save_btn.setObjectName("primaryButton") # Use ID for styling
        save_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        save_btn.clicked.connect(self._save_settings)
        save_btn.setMinimumHeight(35)
        
        cancel_btn = QPushButton("Annulla")
        cancel_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        cancel_btn.clicked.connect(self.reject)
        cancel_btn.setMinimumHeight(35)
        
        btn_layout.addStretch()
        btn_layout.addWidget(cancel_btn)
        btn_layout.addWidget(save_btn)
        
        main_layout.addWidget(container)

    def _browse_json(self):
        start_dir = str(Path(self.json_path_edit.text()).parent)
        selected_file, _ = QFileDialog.getOpenFileName(self, "Seleziona File Serie", start_dir, "JSON Files (*.json)")
        if selected_file:
            self.json_path_edit.setText(selected_file)

    def _browse_output(self):
        start_dir = self.output_dir_edit.text()
        selected_dir = QFileDialog.getExistingDirectory(self, "Seleziona Cartella Output", start_dir)
        if selected_dir:
            self.output_dir_edit.setText(selected_dir)

    def _save_settings(self):
        # Save Paths
        new_json_path = self.json_path_edit.text()
        new_output_dir = self.output_dir_edit.text()

        self.config_manager.set("json_file_path", new_json_path)
        self.config_manager.set("output_dir", new_output_dir)

        if new_json_path != self.initial_json_path:
             self.config_manager.set("is_json_path_customized", True)
             self.paths_changed = True
        
        if new_output_dir != self.initial_output_dir:
            self.paths_changed = True

        # Save Video Settings
        self.config_manager.set("convert_to_h265", self.h265_checkbox.isChecked())
        self.config_manager.set("num_chunks", self.chunk_spin.value())
        
        # Save App Settings (Warning Toggle)
        self.q_settings.setValue("show_stop_warning", self.confirm_stop_checkbox.isChecked())
        
        self.accept()
