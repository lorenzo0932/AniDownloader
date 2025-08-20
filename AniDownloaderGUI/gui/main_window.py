import os
from pathlib import Path
from PyQt6.QtWidgets import (
    QMainWindow, QVBoxLayout, QHBoxLayout, QWidget,
    QPushButton, QTableWidget, QHeaderView, QFileDialog, QLabel,
    QLineEdit, QMessageBox, QTextEdit, QStyle, QMenuBar, QSplitter, QTableWidgetItem, QApplication, QCheckBox
)
from PyQt6.QtCore import QThread, Qt, QSettings, QByteArray
from PyQt6.QtGui import QIcon, QFont, QColor, QAction
from core.download_worker import DownloadWorker
from anidownloader_core.series_repository import SeriesRepository
from anidownloader_config.app_config_manager import AppConfigManager
from anidownloader_config.defaults import DEFAULT_CONFIG_DIR, DEFAULT_SERIES_JSON_PATH
from utils.image_loader import load_poster_image
from .widgets import StatusTableWidgetItem, StopConfirmationDialog
from .series_manager import SeriesManagerDialog

class AniDownloaderGUI(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("AniDownloader GUI")
        screen_geometry = QApplication.primaryScreen().geometry()
        window_width, window_height = 1000, 700
        x, y = (screen_geometry.width() - window_width) // 2, (screen_geometry.height() - window_height) // 2
        self.setGeometry(x, y, window_width, window_height)
        self.setMinimumSize(800, 500)
        
        self.setWindowIcon(QIcon('assets/logo.png'))
        
        self.app_config_manager = AppConfigManager()
        
        DEFAULT_CONFIG_DIR.mkdir(parents=True, exist_ok=True)
        qsettings_path = str(DEFAULT_CONFIG_DIR / "AniDownloader.conf")
        self.settings = QSettings(qsettings_path, QSettings.Format.IniFormat)

        self.json_file_path = Path(self.app_config_manager.get("json_file_path", str(DEFAULT_SERIES_JSON_PATH)))
        self.output_dir = Path(self.app_config_manager.get("output_dir"))
        self.log_file_path = Path(self.app_config_manager.get("log_file_path"))
        
        self.series_repository = SeriesRepository(self.json_file_path)
        self._check_series_file()

        self._download_thread, self._download_worker = None, None
        self._series_data = []
        self._init_ui()
        self._load_series_data_into_table()
        self.restore_geometry_and_state()

    def _check_series_file(self):
        is_json_path_customized = self.app_config_manager.get("is_json_path_customized", False)
        if not self.json_file_path.exists():
            if is_json_path_customized:
                QMessageBox.warning(self, "File Serie Non Trovato", 
                                        f"Il file specificato non è stato trovato:\n{self.json_file_path}\n"
                                        f"Verrà ripristinato il percorso di default.")
            self.json_file_path = DEFAULT_SERIES_JSON_PATH
            self.app_config_manager.set("json_file_path", str(self.json_file_path))
            self.app_config_manager.set("is_json_path_customized", False)
            self.series_repository = SeriesRepository(self.json_file_path)
            self.series_repository.save_series_data([])

    def _init_ui(self):
        self._create_menu_bar()
        self._create_main_layout()
        self._create_config_section()
        self._create_conversion_toggle()
        self._create_control_buttons()
        self._create_series_table()
        self._create_log_output()
        self._setup_main_splitter()
        self._create_overall_status_label()

    def _create_menu_bar(self):
        menu_bar = self.menuBar(); settings_menu = menu_bar.addMenu("Impostazioni")
        reset_warning_action = QAction("Ripristina avviso 'Ferma Download'", self); reset_warning_action.triggered.connect(self._reset_stop_warning_setting); settings_menu.addAction(reset_warning_action)

    def _create_main_layout(self):
        central_widget = QWidget(); self.setCentralWidget(central_widget); self.main_layout = QVBoxLayout(central_widget)
        self.top_container = QWidget(); self.top_layout = QVBoxLayout(self.top_container); self.top_layout.setContentsMargins(0,0,0,0)

    def _create_config_section(self):
        config_group_layout = QVBoxLayout(); json_layout = QHBoxLayout()
        json_label, self.json_path_input = QLabel("File JSON Serie:"), QLineEdit(str(self.json_file_path)); self.json_path_input.setReadOnly(True)
        self.json_browse_button = QPushButton("Sfoglia..."); self.json_browse_button.clicked.connect(self._browse_json_file)
        json_layout.addWidget(json_label); json_layout.addWidget(self.json_path_input); json_layout.addWidget(self.json_browse_button); config_group_layout.addLayout(json_layout)
        output_layout = QHBoxLayout(); output_label, self.output_dir_input = QLabel("Cartella Output:"), QLineEdit(str(self.output_dir)); self.output_dir_input.setReadOnly(True)
        self.output_browse_button = QPushButton("Sfoglia..."); self.output_browse_button.clicked.connect(self._browse_output_dir)
        output_layout.addWidget(output_label); output_layout.addWidget(self.output_dir_input); output_layout.addWidget(self.output_browse_button); config_group_layout.addLayout(output_layout)
        self.top_layout.addLayout(config_group_layout); self.top_layout.addSpacing(10)

    def _create_conversion_toggle(self):
        self.convert_h265_checkbox = QCheckBox("Abilita Conversione H.265 (HEVC)"); self.convert_h265_checkbox.clicked.connect(self._save_conversion_setting)
        initial_state = self.app_config_manager.get("convert_to_h265", False); self.convert_h265_checkbox.setChecked(initial_state)
        conversion_layout = QHBoxLayout(); conversion_layout.addStretch(1); conversion_layout.addWidget(self.convert_h265_checkbox); conversion_layout.addStretch(1)
        self.top_layout.addLayout(conversion_layout); self.top_layout.addSpacing(10)

    def _save_conversion_setting(self):
        self.app_config_manager.set("convert_to_h265", self.convert_h265_checkbox.isChecked())

    def _create_control_buttons(self):
        button_layout = QHBoxLayout()
        self.start_button = QPushButton("Avvia Download"); self.start_button.clicked.connect(self.start_download); self.start_button.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;"); self.start_button.setFixedSize(150, 40)
        self.stop_button = QPushButton("Ferma Download"); self.stop_button.clicked.connect(self.stop_download); self.stop_button.setEnabled(False); self.stop_button.setStyleSheet("background-color: #f44336; color: white; font-weight: bold;"); self.stop_button.setFixedSize(150, 40); self.stop_button.setIcon(self.style().standardIcon(QStyle.StandardPixmap.SP_MessageBoxWarning))
        self.refresh_button = QPushButton("Aggiorna Serie"); self.refresh_button.clicked.connect(self._load_series_data_into_table); self.refresh_button.setFixedSize(150, 40)
        self.manage_series_button = QPushButton("Gestisci Serie"); self.manage_series_button.clicked.connect(self._open_series_manager); self.manage_series_button.setFixedSize(150, 40)
        self.reset_sort_button = QPushButton("Ripristina Ordine"); self.reset_sort_button.clicked.connect(self._reset_table_sort); self.reset_sort_button.setFixedSize(150, 40)
        button_layout.addWidget(self.start_button); button_layout.addWidget(self.stop_button); button_layout.addStretch(1); button_layout.addWidget(self.refresh_button); button_layout.addWidget(self.manage_series_button); button_layout.addWidget(self.reset_sort_button)
        self.top_layout.addLayout(button_layout); self.top_layout.addSpacing(10)

    def _create_series_table(self):
        self.table_widget = QTableWidget(); self.table_widget.setColumnCount(2); self.table_widget.setHorizontalHeaderLabels(["Nome Serie", "Stato"])
        header = self.table_widget.horizontalHeader(); header.setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch); header.setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents); header.setMinimumSectionSize(200)
        self.table_widget.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers); self.table_widget.setSortingEnabled(True); self.table_widget.itemSelectionChanged.connect(self._on_series_selected)
        series_display_layout = QHBoxLayout(); series_display_layout.addWidget(self.table_widget)
        self.image_label = QLabel(); self.image_label.setFixedSize(200, 300); self.image_label.setAlignment(Qt.AlignmentFlag.AlignCenter); self.image_label.setStyleSheet("border: 1px solid #ccc; background-color: #f0f0f0;")
        series_display_layout.addWidget(self.image_label); self.top_layout.addLayout(series_display_layout)

    def _create_log_output(self):
        self.log_output = QTextEdit(); self.log_output.setReadOnly(True); self.log_output.setFont(QFont("Monospace", 9))

    def _setup_main_splitter(self):
        self.main_splitter = QSplitter(Qt.Orientation.Vertical); self.main_splitter.addWidget(self.top_container); self.main_splitter.addWidget(self.log_output)
        self.main_splitter.setStretchFactor(0, 1); self.main_splitter.setStretchFactor(1, 0); self.main_layout.addWidget(self.main_splitter)

    def _create_overall_status_label(self):
        self.overall_status_label = QLabel("Pronto."); self.overall_status_label.setFont(QFont("Sans Serif", 10, QFont.Weight.Bold)); self.main_layout.addWidget(self.overall_status_label)
        
    def restore_geometry_and_state(self):
        geometry = self.settings.value("geometry")
        if geometry:
            self.restoreGeometry(geometry)

        splitter_sizes_value = self.settings.value("splitter_sizes")
        if splitter_sizes_value:
            if isinstance(splitter_sizes_value, QByteArray):
                self.main_splitter.restoreState(splitter_sizes_value)
            elif isinstance(splitter_sizes_value, list):
                try:
                    byte_string = "".join(splitter_sizes_value).encode('ascii')
                    self.main_splitter.restoreState(QByteArray.fromHex(byte_string))
                except Exception:
                    self.main_splitter.setSizes([self.height() - 200, 200])
            else:
                 self.main_splitter.setSizes([self.height() - 200, 200])
        else:
            self.main_splitter.setSizes([self.height() - 200, 200])

    def closeEvent(self, event):
        self.settings.setValue("geometry", self.saveGeometry())
        self.settings.setValue("splitter_sizes", self.main_splitter.saveState())
        super().closeEvent(event)

    def _reset_stop_warning_setting(self):
        self.settings.setValue("show_stop_warning", True); QMessageBox.information(self, "Impostazioni", "L'avviso di interruzione verrà mostrato di nuovo.")

    def _open_series_manager(self):
        dialog = SeriesManagerDialog(series_repository=self.series_repository, parent=self); 
        if dialog.exec(): self._load_series_data_into_table()

    def _browse_json_file(self):
        selected_file, _ = QFileDialog.getOpenFileName(self, "Seleziona File Serie", str(self.json_file_path.parent), "JSON Files (*.json)")
        if selected_file:
            self.json_file_path = Path(selected_file); self.json_path_input.setText(selected_file)
            self.app_config_manager.set("json_file_path", selected_file); self.app_config_manager.set("is_json_path_customized", True)
            self.series_repository = SeriesRepository(self.json_file_path); self._load_series_data_into_table()

    def _browse_output_dir(self):
        selected_dir = QFileDialog.getExistingDirectory(self, "Seleziona Cartella Output", str(self.output_dir))
        if selected_dir:
            self.output_dir = Path(selected_dir); self.output_dir_input.setText(selected_dir); self.app_config_manager.set("output_dir", selected_dir)

    def _load_series_data_into_table(self):
        try: self._series_data = self.series_repository.load_series_data()
        except Exception as e: QMessageBox.critical(self, "Errore Caricamento Serie", f"Impossibile caricare: {e}"); self._series_data = []
        self._populate_table_main_gui(self._series_data)
        # Il reset dell'ordinamento è gestito da _reset_table_sort

    def _populate_table_main_gui(self, data_to_display):
        self.table_widget.setSortingEnabled(False) # Disabilita l'ordinamento durante il popolamento
        self.table_widget.setRowCount(0); self.table_widget.setRowCount(len(data_to_display))
        for row, series in enumerate(data_to_display):
            name_item = QTableWidgetItem(series["name"]); status_item = StatusTableWidgetItem("In attesa", 3)
            self.table_widget.setItem(row, 0, name_item); self.table_widget.setItem(row, 1, status_item)
        if data_to_display: self.table_widget.selectRow(0)
        else: self._on_series_selected()
        self.table_widget.setSortingEnabled(True)

    # MODIFICA 1: Ripristinata la logica corretta per il reset
    def _reset_table_sort(self):
        # Rimuove l'indicatore grafico e ripopola la tabella per ripristinare l'ordine originale
        self.table_widget.horizontalHeader().setSortIndicator(-1, Qt.SortOrder.AscendingOrder)
        self._populate_table_main_gui(self._series_data)

    def _on_series_selected(self):
        if not self.table_widget.selectedItems(): self.image_label.clear(); self.image_label.setText("Nessuna serie selezionata"); return
        row = self.table_widget.currentRow(); 
        if row == -1: return
        item = self.table_widget.item(row, 0)
        if not item: return
        series = next((s for s in self._series_data if s.get("name") == item.text()), None)
        if series and series.get("path"): load_poster_image(self.image_label, series.get("path"))
        else: self.image_label.clear(); self.image_label.setText("Percorso non definito")

    def start_download(self):
        if self._download_thread and self._download_thread.isRunning(): return
        if not self._series_data: QMessageBox.information(self, "Nessuna Serie", "Aggiungi almeno una serie."); return

        self._set_ui_state_for_download(True)
        self.log_output.clear()
        self.overall_status_label.setText("Avvio processo...")
        
        convert_to_h265 = self.convert_h265_checkbox.isChecked()
        
        self._download_thread = QThread()
        self._download_worker = DownloadWorker(
            series_list=self._series_data, 
            json_file_path=self.json_file_path, 
            log_file_path=self.log_file_path, 
            output_dir=self.output_dir, 
            convert_to_h265=convert_to_h265
        )
        self._download_worker.moveToThread(self._download_thread)

        self._download_thread.started.connect(self._download_worker.run)
        self._download_worker._signals.progress.connect(self._update_series_status)
        self._download_worker._signals.error.connect(self._handle_worker_error)
        self._download_worker._signals.finished.connect(self._handle_series_finished)
        self._download_worker._signals.task_skipped.connect(self._handle_task_skipped)
        self._download_worker._signals.overall_status.connect(self.overall_status_label.setText)
        self._download_worker._signals.overall_status.connect(self.log_output.append)
        self._download_thread.finished.connect(self._on_download_finished)
        
        self.table_widget.sortByColumn(1, Qt.SortOrder.AscendingOrder)
        self._download_thread.start()

    def stop_download(self):
        show_warning = self.settings.value("show_stop_warning", True, type=bool)
        if show_warning:
            dialog = StopConfirmationDialog(self);
            if dialog.exec():
                if dialog.dont_show_again(): self.settings.setValue("show_stop_warning", False)
                self._execute_stop_procedure()
        else: self._execute_stop_procedure()

    def _execute_stop_procedure(self):
        if self._download_worker:
            self.overall_status_label.setText("Interruzione in corso..."); self.stop_button.setEnabled(False); self._download_worker.request_stop()

    # MODIFICA 3: Implementato l'ordinamento intelligente
    def _update_series_status(self, series_name, status_message):
        status_lower = status_message.lower()
        new_priority = 1
        color = QColor(Qt.GlobalColor.transparent)

        if "download" in status_lower: new_priority, color = 0, QColor("#D4EDDA")
        elif "conversione" in status_lower: new_priority, color = 0, QColor("#D1ECF1")
        elif "fatto" in status_lower: new_priority, color = 2, QColor("#C3E6CB")
        elif "saltato" in status_lower: new_priority, color = 3, QColor(Qt.GlobalColor.transparent)
        elif "errore" in status_lower: new_priority, color = 1, QColor("#F8D7DA")
        elif "interrotto" in status_lower: new_priority, color = 1, QColor("#F8D7DA")
        
        for row in range(self.table_widget.rowCount()):
            if self.table_widget.item(row, 0).text() == series_name:
                # Controlla la priorità attuale prima di aggiornare
                current_item = self.table_widget.item(row, 1)
                old_priority = -1 # Valore di default se l'item non esiste o non ha priorità
                if isinstance(current_item, StatusTableWidgetItem):
                    old_priority = current_item.priority

                # Aggiorna la riga
                status_item = StatusTableWidgetItem(status_message, new_priority)
                self.table_widget.setItem(row, 1, status_item)
                for col in range(self.table_widget.columnCount()):
                    self.table_widget.item(row, col).setBackground(color)
                
                # Se la priorità è cambiata, scatena un ri-ordinamento
                if old_priority != new_priority:
                    self.table_widget.sortItems(1, Qt.SortOrder.AscendingOrder)
                break
        
    def _handle_worker_error(self, series_name, error_message):
        if series_name in ["GLOBAL", "DEPENDENCIES", "CONFIG"]:
            QMessageBox.critical(self, f"Errore Critico: {series_name}", error_message); self._execute_stop_procedure()
        else: self._update_series_status(series_name, f"❌ Errore")
        self.log_output.append(f"ERRORE [{series_name}]: {error_message}")

    def _handle_series_finished(self, series_name, episode_path, download_time, conversion_time):
        self.log_output.append(f"✅ {os.path.basename(episode_path)} | DL: {download_time:.2f}s | Conv: {conversion_time:.2f}s")
        self._update_series_status(series_name, "✅ Fatto")

    def _handle_task_skipped(self, series_name, reason):
        self.log_output.append(f"🚫 SKIPPED [{series_name}]: {reason}"); self._update_series_status(series_name, f"🚫 Saltato")

    # MODIFICA 2: Aggiunto reset_sort_button alla logica
    def _set_ui_state_for_download(self, in_progress: bool):
        self.table_widget.setSortingEnabled(not in_progress)
        self.main_splitter.setSizes([self.height() - 200, 200] if in_progress else [self.height(), 0])
        
        if in_progress:
            for row in range(self.table_widget.rowCount()):
                self.table_widget.setItem(row, 1, StatusTableWidgetItem("In coda...", 2))
                for col in range(self.table_widget.columnCount()):
                    self.table_widget.item(row, col).setBackground(QColor(Qt.GlobalColor.transparent))

        self.start_button.setEnabled(not in_progress)
        self.stop_button.setEnabled(in_progress)
        self.refresh_button.setEnabled(not in_progress)
        self.manage_series_button.setEnabled(not in_progress)
        self.json_browse_button.setEnabled(not in_progress)
        self.output_browse_button.setEnabled(not in_progress)
        self.convert_h265_checkbox.setEnabled(not in_progress)
        self.reset_sort_button.setEnabled(not in_progress)

    def _on_download_finished(self):
        self._set_ui_state_for_download(False)
        if "Interruzione" not in self.overall_status_label.text():
             self.overall_status_label.setText("Processo completato.")
        if self._download_thread:
            self._download_thread.quit(); self._download_thread.wait()
        self._download_thread, self._download_worker = None, None