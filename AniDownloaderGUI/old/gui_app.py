import sys
import os
import platform
import re

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QVBoxLayout, QHBoxLayout, QWidget,
    QPushButton, QTableWidget, QTableWidgetItem, QHeaderView,
    QFileDialog, QLabel, QLineEdit, QMessageBox, QSizePolicy,
    QTextEdit, QComboBox, QDialog, QCheckBox, QStyle, QMenuBar,
    QFormLayout, QSpinBox
)
from PyQt6.QtCore import QThread, Qt, QSettings, QTimer
from PyQt6.QtGui import QIcon, QFont, QPixmap, QColor, QAction

from download_core import DownloadWorker, DEFAULT_JSON_FILE_PATH, DEFAULT_LOG_FILE, DEFAULT_OUTPUT_DIR

# CLASSE PER L'ORDINAMENTO PERSONALIZZATO DELLO STATO
class StatusTableWidgetItem(QTableWidgetItem):
    def __init__(self, text, priority):
        super().__init__(text)
        self.priority = priority

    def __lt__(self, other):
        if isinstance(other, StatusTableWidgetItem):
            return self.priority < other.priority
        return super().__lt__(other)

# CLASSE PER IL DIALOGO DI CONFERMA DELL'INTERRUZIONE
class StopConfirmationDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Conferma Interruzione")
        self.setModal(True)
        self.setMinimumWidth(400)
        
        layout = QVBoxLayout(self)
        
        message_label = QLabel(
            "<b>ATTENZIONE: Stai per interrompere il processo.</b><br><br>"
            "Questa operazione terminerà forzatamente tutti i download e le conversioni in corso.<br>"
            "Tutti i file parziali e temporanei verranno eliminati.<br><br>"
            "Sei sicuro di voler procedere?"
        )
        message_label.setWordWrap(True)
        layout.addWidget(message_label)

        self.dont_show_again_checkbox = QCheckBox("Non mostrare più questo avviso")
        layout.addWidget(self.dont_show_again_checkbox)

        button_layout = QHBoxLayout()
        self.ok_button = QPushButton("Attendi 5s...")
        self.ok_button.setEnabled(False)
        self.ok_button.clicked.connect(self.accept)
        
        cancel_button = QPushButton("Annulla")
        cancel_button.clicked.connect(self.reject)
        
        button_layout.addStretch()
        button_layout.addWidget(cancel_button)
        button_layout.addWidget(self.ok_button)
        layout.addLayout(button_layout)
        
        self.timer_seconds = 5
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.update_timer)
        self.timer.start(1000)

    def update_timer(self):
        self.timer_seconds -= 1
        if self.timer_seconds > 0:
            self.ok_button.setText(f"Attendi {self.timer_seconds}s...")
        else:
            self.timer.stop()
            self.ok_button.setText("OK, Interrompi")
            self.ok_button.setEnabled(True)

    def dont_show_again(self):
        return self.dont_show_again_checkbox.isChecked()


class AniDownloaderGUI(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("AniDownloader GUI")
        self.setGeometry(100, 100, 900, 600)
        self.setWindowIcon(QIcon('logo.png'))

        self.settings = QSettings("MyScript", "AniDownloader")
        self.json_file_path = self.settings.value("json_file_path", DEFAULT_JSON_FILE_PATH)
        self.output_dir = self.settings.value("output_dir", DEFAULT_OUTPUT_DIR)
        self.log_file_path = self.settings.value("log_file_path", DEFAULT_LOG_FILE)

        self.download_thread = None
        self.download_worker = None
        self.series_data = []

        self._init_ui()
        self._load_series_data_into_table()

    def _init_ui(self):
        menu_bar = self.menuBar()
        settings_menu = menu_bar.addMenu("Impostazioni")
        
        reset_warning_action = QAction("Ripristina avviso 'Ferma Download'", self)
        reset_warning_action.triggered.connect(self._reset_stop_warning_setting)
        settings_menu.addAction(reset_warning_action)

        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QVBoxLayout(central_widget)

        config_group_layout = QVBoxLayout()
        json_layout = QHBoxLayout()
        json_label = QLabel("File JSON Serie:")
        self.json_path_input = QLineEdit(self.json_file_path)
        self.json_path_input.setReadOnly(True)
        json_button = QPushButton("Sfoglia...")
        json_button.clicked.connect(self._browse_json_file)
        json_layout.addWidget(json_label); json_layout.addWidget(self.json_path_input); json_layout.addWidget(json_button)
        config_group_layout.addLayout(json_layout)
        output_layout = QHBoxLayout()
        output_label = QLabel("Cartella Output:")
        self.output_dir_input = QLineEdit(self.output_dir)
        self.output_dir_input.setReadOnly(True)
        output_button = QPushButton("Sfoglia...")
        output_button.clicked.connect(self._browse_output_dir)
        output_layout.addWidget(output_label); output_layout.addWidget(self.output_dir_input); output_layout.addWidget(output_button)
        config_group_layout.addLayout(output_layout)
        main_layout.addLayout(config_group_layout)
        main_layout.addSpacing(10)

        button_layout = QHBoxLayout()
        self.start_button = QPushButton("Avvia Download"); self.start_button.clicked.connect(self.start_download)
        self.start_button.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;"); self.start_button.setFixedSize(150, 40)
        
        self.stop_button = QPushButton("Ferma Download"); self.stop_button.clicked.connect(self.stop_download)
        self.stop_button.setEnabled(False); self.stop_button.setStyleSheet("background-color: #f44336; color: white; font-weight: bold;"); self.stop_button.setFixedSize(150, 40)
        danger_icon = self.style().standardIcon(QStyle.StandardPixmap.SP_MessageBoxWarning)
        self.stop_button.setIcon(danger_icon)

        self.refresh_button = QPushButton("Aggiorna Serie"); self.refresh_button.clicked.connect(self._load_series_data_into_table); self.refresh_button.setFixedSize(150, 40)
        self.manage_series_button = QPushButton("Gestisci Serie"); self.manage_series_button.clicked.connect(self._open_series_manager); self.manage_series_button.setFixedSize(150, 40)
        self.reset_sort_button = QPushButton("Ripristina Ordine"); self.reset_sort_button.clicked.connect(self._reset_table_sort); self.reset_sort_button.setFixedSize(150, 40)
        
        button_layout.addWidget(self.start_button); button_layout.addWidget(self.stop_button); button_layout.addStretch(1)
        button_layout.addWidget(self.refresh_button); button_layout.addWidget(self.manage_series_button); button_layout.addWidget(self.reset_sort_button)
        main_layout.addLayout(button_layout)
        main_layout.addSpacing(10)

        self.table_widget = QTableWidget()
        self.table_widget.setColumnCount(2)
        self.table_widget.setHorizontalHeaderLabels(["Nome Serie", "Stato"])
        self.table_widget.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        self.table_widget.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        self.table_widget.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.table_widget.setSortingEnabled(True)
        self.table_widget.itemSelectionChanged.connect(self._on_series_selected)
        series_display_layout = QHBoxLayout()
        series_display_layout.addWidget(self.table_widget)
        self.image_label = QLabel()
        self.image_label.setFixedSize(200, 300); self.image_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.image_label.setStyleSheet("border: 1px solid #ccc; background-color: #f0f0f0;")
        series_display_layout.addWidget(self.image_label)
        main_layout.addLayout(series_display_layout)

        self.log_output = QTextEdit(); self.log_output.setReadOnly(True); self.log_output.setFont(QFont("Monospace", 9)); self.log_output.setFixedHeight(100)
        main_layout.addWidget(self.log_output)
        self.overall_status_label = QLabel("Pronto."); self.overall_status_label.setFont(QFont("Sans Serif", 10, QFont.Weight.Bold))
        main_layout.addWidget(self.overall_status_label)

    def _reset_stop_warning_setting(self):
        self.settings.setValue("show_stop_warning", True)
        QMessageBox.information(self, "Impostazioni", "L'avviso di interruzione verrà mostrato di nuovo.")

    def _check_path_format(self, path):
        #... (Invariato)
        return True, ""

    def _open_series_manager(self):
        dialog = SeriesManagerDialog(self.json_file_path, self.log_file_path, self)
        if dialog.exec():
            self._load_series_data_into_table()

    def _browse_json_file(self):
        file_dialog = QFileDialog(self)
        file_dialog.setWindowTitle("Seleziona File JSON Serie")
        file_dialog.setFileMode(QFileDialog.FileMode.ExistingFile)
        file_dialog.setNameFilter("JSON Files (*.json)")
        if file_dialog.exec():
            selected_file = file_dialog.selectedFiles()[0]
            self.json_file_path = selected_file
            self.json_path_input.setText(selected_file)
            self.settings.setValue("json_file_path", selected_file)
            self._load_series_data_into_table()

    def _browse_output_dir(self):
        dir_dialog = QFileDialog(self)
        dir_dialog.setWindowTitle("Seleziona Cartella Output")
        dir_dialog.setFileMode(QFileDialog.FileMode.Directory)
        dir_dialog.setOption(QFileDialog.Option.ShowDirsOnly, True)
        if dir_dialog.exec():
            selected_dir = dir_dialog.selectedFiles()[0]
            self.output_dir = selected_dir
            self.output_dir_input.setText(selected_dir)
            self.settings.setValue("output_dir", selected_dir)

    def _load_series_data_into_table(self):
        try:
            worker = DownloadWorker(series_list=[], json_file_path=self.json_file_path, log_file_path=self.log_file_path, output_dir=self.output_dir)
            self.series_data = worker._load_series_data()
        except Exception as e:
            QMessageBox.critical(self, "Errore Caricamento Serie", f"Impossibile caricare i dati: {e}")
            self.series_data = []
        
        self._populate_table_main_gui(self.series_data)
        self.table_widget.horizontalHeader().setSortIndicator(-1, Qt.SortOrder.AscendingOrder)
        
        path_warnings = [f"Percorso per '{s.get('name', 'N/D')}': {msg}" for s in self.series_data if not (is_ok := self._check_path_format(s.get("path", "")))[0] and (msg := is_ok[1])]
        if path_warnings:
            QMessageBox.warning(self, "Avviso Formattazione Percorsi", "Problemi di formattazione rilevati:\n\n" + "\n\n".join(path_warnings))

    def _populate_table_main_gui(self, data_to_display):
        self.table_widget.setRowCount(0)
        self.table_widget.setRowCount(len(data_to_display))
        for row, series in enumerate(data_to_display):
            self.table_widget.setItem(row, 0, QTableWidgetItem(series["name"]))
            self.table_widget.setItem(row, 1, StatusTableWidgetItem("In attesa", 3))
        if data_to_display: self.table_widget.selectRow(0)
        else: self._on_series_selected()

    def _reset_table_sort(self):
        self.table_widget.horizontalHeader().setSortIndicator(-1, Qt.SortOrder.AscendingOrder)
        self._populate_table_main_gui(self.series_data)

    def _on_series_selected(self):
        selected_items = self.table_widget.selectedItems()
        if not selected_items:
            self.image_label.clear(); return
        row = selected_items[0].row()
        selected_name_item = self.table_widget.item(row, 0)
        if selected_name_item is None:
            self.image_label.clear(); return
        selected_name = selected_name_item.text()
        series = next((s for s in self.series_data if s.get("name") == selected_name), None)
        if series and series.get("path"):
            image_path = os.path.join(os.path.dirname(series.get("path")), "folder.jpg")
            if os.path.exists(image_path):
                pixmap = QPixmap(image_path)
                if not pixmap.isNull():
                    self.image_label.setPixmap(pixmap.scaled(self.image_label.size(), Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation))
                else: self.image_label.clear(); self.image_label.setText("Immagine non valida")
            else: self.image_label.clear(); self.image_label.setText("folder.jpg non trovato")
        else: self.image_label.clear(); self.image_label.setText("Percorso non definito")

    def start_download(self):
        if self.download_thread and self.download_thread.isRunning(): return
        if not self.series_data: return

        for row in range(self.table_widget.rowCount()):
            self.table_widget.setItem(row, 1, StatusTableWidgetItem("In coda...", 2))
            self.table_widget.item(row, 1).setBackground(QColor(Qt.GlobalColor.transparent))

        self.start_button.setEnabled(False); self.stop_button.setEnabled(True)
        self.refresh_button.setEnabled(False); self.json_path_input.setEnabled(False); self.output_dir_input.setEnabled(False)
        self.log_output.clear(); self.overall_status_label.setText("Avvio processo...")

        self.download_thread = QThread()
        self.download_worker = DownloadWorker(series_list=self.series_data, json_file_path=self.json_file_path, log_file_path=self.log_file_path, output_dir=self.output_dir)
        self.download_worker.moveToThread(self.download_thread)
        self.download_thread.started.connect(self.download_worker.run)
        self.download_worker.signals.progress.connect(self._update_series_status)
        self.download_worker.signals.error.connect(self._handle_worker_error)
        self.download_worker.signals.finished.connect(self._handle_series_finished)
        self.download_worker.signals.task_skipped.connect(self._handle_task_skipped)
        self.download_worker.signals.overall_status.connect(self.overall_status_label.setText)
        self.download_worker.signals.overall_status.connect(self.log_output.append)
        self.download_thread.finished.connect(self._download_finished)
        
        self.table_widget.sortByColumn(1, Qt.SortOrder.AscendingOrder)
        self.download_thread.start()

    def stop_download(self):
        show_warning = self.settings.value("show_stop_warning", True, type=bool)
        if show_warning:
            dialog = StopConfirmationDialog(self)
            if dialog.exec():
                if dialog.dont_show_again():
                    self.settings.setValue("show_stop_warning", False)
                self._execute_stop_procedure()
        else:
            self._execute_stop_procedure()

    def _execute_stop_procedure(self):
        if self.download_worker:
            self.overall_status_label.setText("Interruzione in corso...")
            self.stop_button.setEnabled(False)
            self.download_worker.request_stop()

    def _update_series_status(self, series_name, status_message):
        status_lower = status_message.lower()
        priority = 1
        color = QColor(Qt.GlobalColor.transparent)

        if "download" in status_lower:
            priority = 0
            color = QColor(200, 255, 200) # Verde chiaro
        elif "conversione" in status_lower:
            priority = 0
            color = QColor(200, 220, 255) # Azzurro chiaro
        elif "interrotto" in status_lower:
            priority = 1
            color = QColor(255, 200, 200) # Rosso chiaro per interruzione
        
        for row in range(self.table_widget.rowCount()):
            if self.table_widget.item(row, 0).text() == series_name:
                status_item = StatusTableWidgetItem(status_message, priority)
                status_item.setBackground(color)
                self.table_widget.setItem(row, 1, status_item)
                break

    def _handle_worker_error(self, series_name, error_message):
        if series_name in ["GLOBAL", "DEPENDENCIES", "CONFIG"]:
            self.log_output.append(f"ERRORE CRITICO ({series_name}): {error_message}")
            QMessageBox.critical(self, f"Errore {series_name}", error_message)
            self._execute_stop_procedure()
        else:
            self.log_output.append(f"ERRORE [{series_name}]: {error_message}")
            self._update_series_status(series_name, f"❌ Errore")

    def _handle_series_finished(self, series_name, episode_path, download_time, conversion_time):
        self.log_output.append(f"✅ {os.path.basename(episode_path)} | DL: {download_time:.2f}s | Conv: {conversion_time:.2f}s")
        self._update_series_status(series_name, "✅ Fatto")

    def _handle_task_skipped(self, series_name, reason):
        self.log_output.append(f"🚫 SKIPPED [{series_name}]: {reason}")
        self._update_series_status(series_name, f"🚫 Saltato: {reason}")

    def _download_finished(self):
        self.start_button.setEnabled(True); self.stop_button.setEnabled(False)
        self.refresh_button.setEnabled(True); self.json_path_input.setEnabled(True); self.output_dir_input.setEnabled(True)
        if "Interruzione" not in self.overall_status_label.text():
             self.overall_status_label.setText("Processo completato.")
        
        if self.download_thread:
            self.download_thread.quit()
            self.download_thread.wait()
        self.download_thread = None
        self.download_worker = None

# CLASSE PER LA GESTIONE DELLE SERIE (INVARIATA MA INCLUSA)
class SeriesManagerDialog(QDialog):
    def __init__(self, json_file_path, log_file_path, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Gestisci Serie")
        self.setGeometry(200, 200, 800, 600)
        self.json_file_path = json_file_path
        self.log_file_path = log_file_path
        self.series_data = []
        self.original_series_data = []
        self._init_ui()
        self._load_series_data()

    def _init_ui(self):
        main_layout = QVBoxLayout(self)
        self.table_widget = QTableWidget()
        self.table_widget.setColumnCount(5)
        self.table_widget.setHorizontalHeaderLabels(["Nome", "Percorso", "Pattern Link", "Continua", "Ep. Passati"])
        self.table_widget.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        self.table_widget.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.table_widget.setSelectionMode(QTableWidget.SelectionMode.SingleSelection)
        self.table_widget.setSortingEnabled(True)
        main_layout.addWidget(self.table_widget)
        
        input_group_layout = QFormLayout()
        self.name_input = QLineEdit()
        self.path_input = QLineEdit()
        
        path_layout = QHBoxLayout()
        path_layout.addWidget(self.path_input)
        self.path_browse_button = QPushButton("Sfoglia...")
        self.path_browse_button.clicked.connect(self._browse_series_path)
        path_layout.addWidget(self.path_browse_button)

        self.link_pattern_input = QLineEdit()
        self.continue_checkbox = QCheckBox()
        self.passed_episodes_input = QSpinBox()
        self.passed_episodes_input.setMinimum(0)
        self.passed_episodes_input.setMaximum(999)
        
        input_group_layout.addRow("Nome:", self.name_input)
        input_group_layout.addRow("Percorso:", path_layout)
        input_group_layout.addRow("Pattern Link:", self.link_pattern_input)
        input_group_layout.addRow("Continua:", self.continue_checkbox)
        input_group_layout.addRow("Ep. Passati:", self.passed_episodes_input)
        main_layout.addLayout(input_group_layout)
        
        button_layout = QHBoxLayout()
        self.add_button = QPushButton("Aggiungi Serie"); self.add_button.clicked.connect(self._add_series)
        self.remove_button = QPushButton("Rimuovi Selezionata"); self.remove_button.clicked.connect(self._remove_selected_series)
        self.save_button = QPushButton("Salva"); self.save_button.clicked.connect(self._save_changes)
        self.cancel_button = QPushButton("Annulla"); self.cancel_button.clicked.connect(self.reject)
        button_layout.addWidget(self.add_button); button_layout.addWidget(self.remove_button); button_layout.addStretch(1); button_layout.addWidget(self.save_button); button_layout.addWidget(self.cancel_button)
        main_layout.addLayout(button_layout)

    def _browse_series_path(self):
        dir_dialog = QFileDialog(self)
        dir_dialog.setWindowTitle("Seleziona Cartella per la Serie")
        dir_dialog.setFileMode(QFileDialog.FileMode.Directory)
        dir_dialog.setOption(QFileDialog.Option.ShowDirsOnly, True)
        if dir_dialog.exec():
            selected_dir = dir_dialog.selectedFiles()[0]
            self.path_input.setText(selected_dir)

    def _load_series_data(self):
        try:
            worker = DownloadWorker(series_list=[], json_file_path=self.json_file_path, log_file_path=self.log_file_path)
            self.series_data = worker._load_series_data()
            self.original_series_data = [s.copy() for s in self.series_data]
            self._populate_table()
        except Exception as e:
            QMessageBox.critical(self, "Errore Caricamento", f"Impossibile caricare: {e}")

    def _populate_table(self):
        self.table_widget.setRowCount(0)
        self.table_widget.setRowCount(len(self.series_data))
        for row, series in enumerate(self.series_data):
            self.table_widget.setItem(row, 0, QTableWidgetItem(series.get("name", "")))
            self.table_widget.setItem(row, 1, QTableWidgetItem(series.get("path", "")))
            self.table_widget.setItem(row, 2, QTableWidgetItem(series.get("link_pattern", "")))
            self.table_widget.setItem(row, 3, QTableWidgetItem("Sì" if series.get("continue", False) else "No"))
            self.table_widget.setItem(row, 4, QTableWidgetItem(str(series.get("passed_episodes", 0))))

    def _add_series(self):
        name = self.name_input.text().strip()
        path = self.path_input.text().strip()
        link_pattern = self.link_pattern_input.text().strip()
        if not all([name, path, link_pattern]):
            QMessageBox.warning(self, "Input Mancanti", "Nome, Percorso e Pattern sono obbligatori."); return
        new_series = {"name": name, "path": path, "link_pattern": link_pattern}
        if self.continue_checkbox.isChecked():
            new_series["continue"] = True
            new_series["passed_episodes"] = self.passed_episodes_input.value()
        self.series_data.append(new_series)
        self._populate_table()

    def _remove_selected_series(self):
        selected_rows = self.table_widget.selectionModel().selectedRows()
        if not selected_rows:
            QMessageBox.warning(self, "Nessuna Selezione", "Seleziona una serie da rimuovere."); return
        selected_name = self.table_widget.item(selected_rows[0].row(), 0).text()
        self.series_data = [s for s in self.series_data if s.get("name") != selected_name]
        self._populate_table()

    def _save_changes(self):
        from download_core import save_series_data
        try:
            if self.series_data != self.original_series_data:
                save_series_data(self.json_file_path, self.series_data)
                self.accept()
            else:
                self.reject()
        except Exception as e:
            QMessageBox.critical(self, "Errore Salvataggio", f"Impossibile salvare: {e}")


if __name__ == '__main__':
    app = QApplication(sys.argv)
    app.setStyle("Fusion") 
    window = AniDownloaderGUI()
    window.show()
    sys.exit(app.exec())