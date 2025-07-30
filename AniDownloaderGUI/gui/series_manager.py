import os
import re
import platform
from PyQt6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QTableWidget, QTableWidgetItem,
    QHeaderView, QLabel, QLineEdit, QPushButton, QMessageBox, QApplication
)
from PyQt6.QtCore import Qt
from core.series_repository import SeriesRepository
from utils.image_loader import load_poster_image
from .series_editor import SeriesEditorDialog

class SeriesManagerDialog(QDialog):
    def __init__(self, series_repository: SeriesRepository, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Gestisci Serie")
        screen_geometry = QApplication.primaryScreen().geometry()
        window_width = 1200 
        window_height = 600 
        x = (screen_geometry.width() - window_width) // 2
        y = (screen_geometry.height() - window_height) // 2
        self.setGeometry(x, y, window_width, window_height)
        self.setMinimumSize(700, 500)
        
        self._series_repository = series_repository
        self._series_data = []
        self._original_series_data = []
        self._init_ui()
        self._load_series_data()

    def _save_current_series_data(self):
        try:
            self._series_repository.save_series_data(self._series_data)
            self._original_series_data = [s.copy() for s in self._series_data]
        except Exception as e:
            QMessageBox.critical(self, "Errore Salvataggio Automatico", f"Impossibile salvare le modifiche automaticamente: {e}")

    def _init_ui(self):
        self.main_layout = QVBoxLayout(self)
        self._create_series_display_section()
        self._create_control_section()
        self._create_button_section()

    def _create_series_display_section(self):
        series_display_layout = QHBoxLayout()
        self._table_widget = QTableWidget()
        self._table_widget.setColumnCount(5)
        self._table_widget.setHorizontalHeaderLabels(["Nome", "Percorso", "Pattern Link", "Continua", "Ep. Passati"])
        
        header = self._table_widget.horizontalHeader()
        for i in range(self._table_widget.columnCount() - 2):
            header.setSectionResizeMode(i, QHeaderView.ResizeMode.Stretch)
            
        header.setSectionResizeMode(3, QHeaderView.ResizeMode.ResizeToContents)
        header.setSectionResizeMode(4, QHeaderView.ResizeMode.ResizeToContents)
    
        self._table_widget.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self._table_widget.setSelectionMode(QTableWidget.SelectionMode.SingleSelection)
        self._table_widget.setSortingEnabled(True)
        self._table_widget.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self._table_widget.itemSelectionChanged.connect(self._on_series_selected)
        self._table_widget.doubleClicked.connect(self._open_series_editor)
        series_display_layout.addWidget(self._table_widget)
        
        self._image_label = QLabel("Seleziona una serie per vedere la locandina")
        self._image_label.setFixedSize(200, 300)
        self._image_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._image_label.setStyleSheet("border: 1px solid #ccc; background-color: #f0f0f0;")
        series_display_layout.addWidget(self._image_label)
        self.main_layout.addLayout(series_display_layout)

    def _create_control_section(self):
        control_layout = QHBoxLayout()
        control_layout.addWidget(QLabel("Cerca:"))
        self._search_input = QLineEdit()
        self._search_input.setPlaceholderText("Cerca per nome...")
        self._search_input.textChanged.connect(self._filter_series)
        control_layout.addWidget(self._search_input)
        control_layout.addStretch(1)
        
        self._reset_sort_button = QPushButton("Ripristina Ordine")
        self._reset_sort_button.clicked.connect(self._reset_table_sort)
        control_layout.addWidget(self._reset_sort_button)
        self.main_layout.addLayout(control_layout)

    def _create_button_section(self):
        button_layout = QHBoxLayout()
        self._add_button = QPushButton("Aggiungi Serie")
        self._add_button.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;")
        self._add_button.clicked.connect(self._add_series)
        self._edit_button = QPushButton("Modifica Serie Selezionata")
        self._edit_button.clicked.connect(self._open_series_editor)
        self._remove_button = QPushButton("Rimuovi Selezionata")
        self._remove_button.setStyleSheet("background-color: #f44336; color: white; font-weight: bold;")
        self._remove_button.clicked.connect(self._remove_selected_series)
        self._save_button = QPushButton("Salva e Chiudi")
        self._save_button.setDefault(True)
        self._save_button.clicked.connect(self._save_changes_and_accept)
        self._cancel_button = QPushButton("Annulla")
        self._cancel_button.clicked.connect(self.reject)
        button_layout.addWidget(self._add_button)
        button_layout.addWidget(self._edit_button)
        button_layout.addWidget(self._remove_button)
        button_layout.addStretch(1)
        button_layout.addWidget(self._cancel_button)
        button_layout.addWidget(self._save_button)
        self.main_layout.addLayout(button_layout)

    def _reset_table_sort(self):
        self._table_widget.horizontalHeader().setSortIndicator(-1, Qt.SortOrder.AscendingOrder)
        self._filter_series()

    def _on_series_selected(self):
        selected_items = self._table_widget.selectedItems()
        if not selected_items:
            self._image_label.clear()
            self._image_label.setText("Nessuna serie selezionata")
            return
        row = selected_items[0].row()
        item = self._table_widget.item(row, 0)
        if not item:
            self._image_label.clear()
            self._image_label.setText("Nessuna serie selezionata")
            return
        
        series_name = item.text()
        series = next((s for s in self._series_data if s.get("name") == series_name), None)

        if series and series.get("path"):
            load_poster_image(self._image_label, series.get("path"))
        else:
            self._image_label.clear()
            self._image_label.setText("Percorso non definito")

    def _load_series_data(self):
        try:
            self._series_data = self._series_repository.load_series_data()
            self._original_series_data = [s.copy() for s in self._series_data]
            self._filter_series()
        except Exception as e:
            QMessageBox.critical(self, "Errore Caricamento", f"Impossibile caricare: {e}")

    def _filter_series(self):
        search_text = self._search_input.text().lower()
        filtered_data = [s for s in self._series_data if search_text in s.get("name", "").lower()]
        self._populate_table(filtered_data)

    def _populate_table(self, data):
        self._table_widget.blockSignals(True)
        self._table_widget.setRowCount(0)
        self._table_widget.setRowCount(len(data))
        for row, series in enumerate(data):
            name_item = QTableWidgetItem(series.get("name", ""))
            path_item = QTableWidgetItem(series.get("path", ""))
            link_pattern_item = QTableWidgetItem(series.get("link_pattern", ""))
            continue_item = QTableWidgetItem("Sì" if series.get("continue", False) else "No")
            passed_episodes_item = QTableWidgetItem(str(series.get("passed_episodes", 0)))

            # Set alignment for content of specific columns
            continue_item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
            passed_episodes_item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)

            self._table_widget.setItem(row, 0, name_item)
            self._table_widget.setItem(row, 1, path_item)
            self._table_widget.setItem(row, 2, link_pattern_item)
            self._table_widget.setItem(row, 3, continue_item)
            self._table_widget.setItem(row, 4, passed_episodes_item)
        self._table_widget.blockSignals(False)
        if data: self._table_widget.selectRow(0)
        else: self._on_series_selected()
        
        # **LA SOLUZIONE È QUI (Parte 2):** Applica il layout intelligente dopo aver caricato i dati
        # self._table_widget.resizeColumnsToContents()
        # self._table_widget.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        # self._table_widget.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        # self._table_widget.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeMode.Stretch)

    def _add_series(self):
        editor = SeriesEditorDialog({}, is_new=True, parent=self)
        if editor.exec():
            is_deleted, new_data = editor.get_data()
            if not is_deleted and new_data:
                self._series_data.append(new_data)
                self._filter_series()
                self._save_current_series_data()

    def _open_series_editor(self):
        selected_rows = self._table_widget.selectionModel().selectedRows()
        if not selected_rows:
            QMessageBox.warning(self, "Nessuna Selezione", "Seleziona una serie da modificare."); return
        
        selected_name = self._table_widget.item(selected_rows[0].row(), 0).text()
        series_to_edit = next((s for s in self._series_data if s.get("name") == selected_name), None)
        
        if series_to_edit:
            editor = SeriesEditorDialog(series_to_edit, is_new=False, parent=self)
            if editor.exec():
                is_deleted, modified_data = editor.get_data()
                original_index = next((i for i, s in enumerate(self._series_data) if s["name"] == selected_name), -1)
                if original_index == -1: return

                if is_deleted: self._series_data.pop(original_index)
                elif modified_data: self._series_data[original_index] = modified_data
                self._filter_series()
                self._save_current_series_data()

    def _remove_selected_series(self):
        selected_rows = self._table_widget.selectionModel().selectedRows()
        if not selected_rows:
            QMessageBox.warning(self, "Nessuna Selezione", "Seleziona una serie da rimuovere."); return
        
        selected_name = self._table_widget.item(selected_rows[0].row(), 0).text()
        reply = QMessageBox.question(self, "Conferma Eliminazione", f"Sei sicuro di voler rimuovere '{selected_name}' dalla lista?\nQuesta operazione sarà permanente solo dopo aver salvato.",
                                     QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No, QMessageBox.StandardButton.No)
        if reply == QMessageBox.StandardButton.Yes:
            self._series_data = [s for s in self._series_data if s.get("name") != selected_name]
            self._filter_series()
            self._save_current_series_data()

    def _save_changes_and_accept(self):
        # Changes are now saved automatically by _add_series, _open_series_editor, and _remove_selected_series.
        # This method now primarily serves to close the dialog.
        self.accept()
