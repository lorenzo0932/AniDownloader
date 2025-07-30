import os
from pathlib import Path
from PyQt6.QtWidgets import (
    QMainWindow, QVBoxLayout, QHBoxLayout, QWidget,
    QPushButton, QTableWidget, QHeaderView, QFileDialog, QLabel,
    QLineEdit, QMessageBox, QTextEdit, QStyle, QMenuBar, QSplitter, QTableWidgetItem, QApplication
)
from PyQt6.QtCore import QThread, Qt, QSettings
from PyQt6.QtGui import QIcon, QFont, QColor, QAction
from core.download_worker import DownloadWorker
from core.series_repository import SeriesRepository
from config.app_config_manager import AppConfigManager
from config.defaults import DEFAULT_CONFIG_DIR, DEFAULT_SERIES_JSON_PATH
from utils.image_loader import load_poster_image
from .widgets import StatusTableWidgetItem, StopConfirmationDialog
from .series_manager import SeriesManagerDialog

class AniDownloaderGUI(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("AniDownloader GUI")
        # Calcola le dimensioni e la posizione per centrare la finestra
        screen_geometry = QApplication.primaryScreen().geometry()
        window_width = 1000 
        window_height = 700 
        x = (screen_geometry.width() - window_width) // 2
        y = (screen_geometry.height() - window_height) // 2
        self.setGeometry(x, y, window_width, window_height)
        self.setMinimumSize(800, 500)
        
        self.setWindowIcon(QIcon('assets/logo.png'))
        
        # Inizializzazione del gestore di configurazione
        self.app_config_manager = AppConfigManager()
        
        # Assicurati che la directory di configurazione esista per QSettings
        DEFAULT_CONFIG_DIR.mkdir(parents=True, exist_ok=True)
        qsettings_path = str(DEFAULT_CONFIG_DIR / "AniDownloader.conf")
        self.settings = QSettings(qsettings_path, QSettings.Format.IniFormat) # Per le impostazioni dell'applicazione (es. avvisi)

        # Inizializza tutti i percorsi dai valori di configurazione o dai default
        self.json_file_path = Path(self.app_config_manager.get("json_file_path"))
        self.output_dir = Path(self.app_config_manager.get("output_dir"))
        self.log_file_path = Path(self.app_config_manager.get("log_file_path"))
        
        is_json_path_customized = self.app_config_manager.get("is_json_path_customized", False)

        # Inizializzazione del repository delle serie
        self.series_repository = SeriesRepository(self.json_file_path)

        # Verifica se il file series_data.json esiste al percorso configurato
        if not self.json_file_path.exists():
            # Se il percorso era personalizzato e il file non esiste più, notifica l'utente
            if is_json_path_customized:
                QMessageBox.information(self, "File Serie Non Trovato", 
                                        f"Il file dei dati delle serie non è stato trovato nella posizione specificata:\n"
                                        f"{self.json_file_path}\n"
                                        f"È stato ripristinato il percorso di default e creato un nuovo file vuoto in: {DEFAULT_SERIES_JSON_PATH}")
            
            # Reimposta al percorso di default e aggiorna la configurazione
            self.json_file_path = DEFAULT_SERIES_JSON_PATH
            self.app_config_manager.set("json_file_path", str(self.json_file_path))
            self.app_config_manager.set("is_json_path_customized", False)
            
            # Assicurati che il file series_data.json di default esista
            self.series_repository.save_series_data([]) # Crea un file vuoto

        self._download_thread = None
        self._download_worker = None
        self._series_data = [] # Lasciato pubblico come richiesto
        self._init_ui()
        self._load_series_data_into_table()

    def _init_ui(self):
        self._create_menu_bar()
        self._create_main_layout()
        self._create_config_section()
        self._create_control_buttons()
        self._create_series_table()
        self._create_log_output()
        self._setup_main_splitter()
        self._create_overall_status_label()

    def _create_menu_bar(self):
        menu_bar = self.menuBar()
        settings_menu = menu_bar.addMenu("Impostazioni")
        reset_warning_action = QAction("Ripristina avviso 'Ferma Download'", self)
        reset_warning_action.triggered.connect(self._reset_stop_warning_setting)
        settings_menu.addAction(reset_warning_action)

    def _create_main_layout(self):
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        self.main_layout = QVBoxLayout(central_widget)

        self.top_container = QWidget()
        self.top_layout = QVBoxLayout(self.top_container)
        self.top_layout.setContentsMargins(0,0,0,0)

    def _create_config_section(self):
        config_group_layout = QVBoxLayout()
        
        json_layout = QHBoxLayout()
        json_label = QLabel("File JSON Serie:")
        self.json_path_input = QLineEdit(str(self.json_file_path))
        self.json_path_input.setReadOnly(True)
        json_button = QPushButton("Sfoglia...")
        json_button.clicked.connect(self._browse_json_file)
        json_layout.addWidget(json_label)
        json_layout.addWidget(self.json_path_input)
        json_layout.addWidget(json_button)
        config_group_layout.addLayout(json_layout)

        output_layout = QHBoxLayout()
        output_label = QLabel("Cartella Output:")
        self.output_dir_input = QLineEdit(str(self.output_dir))
        self.output_dir_input.setReadOnly(True)
        output_button = QPushButton("Sfoglia...")
        output_button.clicked.connect(self._browse_output_dir)
        output_layout.addWidget(output_label)
        output_layout.addWidget(self.output_dir_input)
        output_layout.addWidget(output_button)
        config_group_layout.addLayout(output_layout)
        
        self.top_layout.addLayout(config_group_layout)
        self.top_layout.addSpacing(10)

    def _create_control_buttons(self):
        button_layout = QHBoxLayout()
        self.start_button = QPushButton("Avvia Download")
        self.start_button.clicked.connect(self.start_download)
        self.start_button.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;")
        self.start_button.setFixedSize(150, 40)
        
        self.stop_button = QPushButton("Ferma Download")
        self.stop_button.clicked.connect(self.stop_download)
        self.stop_button.setEnabled(False)
        self.stop_button.setStyleSheet("background-color: #f44336; color: white; font-weight: bold;")
        self.stop_button.setFixedSize(150, 40)
        danger_icon = self.style().standardIcon(QStyle.StandardPixmap.SP_MessageBoxWarning)
        self.stop_button.setIcon(danger_icon)
        
        self.refresh_button = QPushButton("Aggiorna Serie")
        self.refresh_button.clicked.connect(self._load_series_data_into_table)
        self.refresh_button.setFixedSize(150, 40)
        
        self.manage_series_button = QPushButton("Gestisci Serie")
        self.manage_series_button.clicked.connect(self._open_series_manager)
        self.manage_series_button.setFixedSize(150, 40)
        
        self.reset_sort_button = QPushButton("Ripristina Ordine")
        self.reset_sort_button.clicked.connect(self._reset_table_sort)
        self.reset_sort_button.setFixedSize(150, 40)
        
        button_layout.addWidget(self.start_button)
        button_layout.addWidget(self.stop_button)
        button_layout.addStretch(1)
        button_layout.addWidget(self.refresh_button)
        button_layout.addWidget(self.manage_series_button)
        button_layout.addWidget(self.reset_sort_button)
        self.top_layout.addLayout(button_layout)
        self.top_layout.addSpacing(10)

    def _create_series_table(self):
        self.table_widget = QTableWidget()
        self.table_widget.setColumnCount(2)
        self.table_widget.setHorizontalHeaderLabels(["Nome Serie", "Stato"])
        header = self.table_widget.horizontalHeader()
        header.setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        header.setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        self.table_widget.horizontalHeaderItem(0).setTextAlignment(Qt.AlignmentFlag.AlignCenter)
        self.table_widget.horizontalHeaderItem(1).setTextAlignment(Qt.AlignmentFlag.AlignCenter)
        self.table_widget.verticalHeader().setDefaultAlignment(Qt.AlignmentFlag.AlignCenter)
        self.table_widget.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.table_widget.setSortingEnabled(True)
        self.table_widget.itemSelectionChanged.connect(self._on_series_selected)
        
        series_display_layout = QHBoxLayout()
        series_display_layout.addWidget(self.table_widget)
        self.image_label = QLabel()
        self.image_label.setFixedSize(200, 300)
        self.image_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.image_label.setStyleSheet("border: 1px solid #ccc; background-color: #f0f0f0;")
        series_display_layout.addWidget(self.image_label)
        self.top_layout.addLayout(series_display_layout)

    def _create_log_output(self):
        self.log_output = QTextEdit()
        self.log_output.setReadOnly(True)
        self.log_output.setFont(QFont("Monospace", 9))

    def _setup_main_splitter(self):
        self.main_splitter = QSplitter(Qt.Orientation.Vertical)
        self.main_splitter.addWidget(self.top_container)
        self.main_splitter.addWidget(self.log_output)
        self.main_splitter.setSizes([450, 0])
        self.main_splitter.setStretchFactor(0, 1)
        self.main_splitter.setStretchFactor(1, 0)
        self.main_layout.addWidget(self.main_splitter)

    def _create_overall_status_label(self):
        self.overall_status_label = QLabel("Pronto.")
        self.overall_status_label.setFont(QFont("Sans Serif", 10, QFont.Weight.Bold))
        self.main_layout.addWidget(self.overall_status_label)

    def _reset_stop_warning_setting(self):
        self.settings.setValue("show_stop_warning", True)
        QMessageBox.information(self, "Impostazioni", "L'avviso di interruzione verrà mostrato di nuovo.")

    def _open_series_manager(self):
        dialog = SeriesManagerDialog(series_repository=self.series_repository, parent=self)
        if dialog.exec():
            self._load_series_data_into_table()

    def _browse_json_file(self):
        file_dialog = QFileDialog(self)
        if file_dialog.exec():
            selected_file = file_dialog.selectedFiles()[0]
            self.json_file_path = Path(selected_file)
            self.json_path_input.setText(selected_file)
            self.app_config_manager.set("json_file_path", selected_file)
            self.app_config_manager.set("is_json_path_customized", True)
            self.series_repository._json_file_path = self.json_file_path # Aggiorna il percorso nel repository
            self._load_series_data_into_table()

    def _browse_output_dir(self):
        dir_dialog = QFileDialog(self)
        if dir_dialog.exec():
            selected_dir = dir_dialog.selectedFiles()[0]
            self.output_dir = Path(selected_dir)
            self.output_dir_input.setText(selected_dir)
            self.app_config_manager.set("output_dir", selected_dir)

    def _load_series_data_into_table(self):
        try:
            self._series_data = self.series_repository.load_series_data()
        except Exception as e:
            QMessageBox.critical(self, "Errore Caricamento Serie", f"Impossibile caricare: {e}")
            self._series_data = []
        self._populate_table_main_gui(self._series_data)
        self.table_widget.horizontalHeader().setSortIndicator(-1, Qt.SortOrder.AscendingOrder)

    def _populate_table_main_gui(self, data_to_display):
        self.table_widget.setRowCount(0)
        self.table_widget.setRowCount(len(data_to_display))
        for row, series in enumerate(data_to_display):
            name_item = QTableWidgetItem(series["name"])
            status_item = StatusTableWidgetItem("In attesa", 3)

            # Set alignment for content of both columns
            name_item.setTextAlignment(Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignVCenter)
            status_item.setTextAlignment(Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignVCenter)

            self.table_widget.setItem(row, 0, name_item)
            self.table_widget.setItem(row, 1, status_item)
        if data_to_display: self.table_widget.selectRow(0)
        else: self._on_series_selected()
        
    def _reset_table_sort(self):
        self.table_widget.horizontalHeader().setSortIndicator(-1, Qt.SortOrder.AscendingOrder)
        self._populate_table_main_gui(self._series_data)

    def _on_series_selected(self):
        selected_items = self.table_widget.selectedItems()
        if not selected_items:
            self.image_label.clear()
            self.image_label.setText("Nessuna serie selezionata")
            return
        row = selected_items[0].row()
        item = self.table_widget.item(row, 0)
        if not item:
            self.image_label.clear()
            self.image_label.setText("Nessuna serie selezionata")
            return
        
        series = next((s for s in self._series_data if s.get("name") == item.text()), None)
        if series and series.get("path"):
            load_poster_image(self.image_label, series.get("path"))
        else:
            self.image_label.clear()
            self.image_label.setText("Percorso non definito")

    def start_download(self):
        if self._download_thread and self._download_thread.isRunning(): return
        if not self._series_data: return

        self._set_download_in_progress_ui_state(True)
        self.log_output.clear()
        self.overall_status_label.setText("Avvio processo...")

        self._download_thread = QThread()
        self._download_worker = DownloadWorker(series_list=self._series_data, json_file_path=self.json_file_path, log_file_path=self.log_file_path, output_dir=self.output_dir)
        self._download_worker.moveToThread(self._download_thread)
        self._download_thread.started.connect(self._download_worker.run)
        self._download_worker._signals.progress.connect(self._update_series_status)
        self._download_worker._signals.error.connect(self._handle_worker_error)
        self._download_worker._signals.finished.connect(self._handle_series_finished)
        self._download_worker._signals.task_skipped.connect(self._handle_task_skipped)
        self._download_worker._signals.overall_status.connect(self.overall_status_label.setText)
        self._download_worker._signals.overall_status.connect(self.log_output.append)
        self._download_thread.finished.connect(self._download_finished)
        
        self.table_widget.sortByColumn(1, Qt.SortOrder.AscendingOrder)
        self._download_thread.start()

    def stop_download(self):
        show_warning = self.settings.value("show_stop_warning", True, type=bool)
        if show_warning:
            dialog = StopConfirmationDialog(self)
            if dialog.exec():
                if dialog.dont_show_again(): self.settings.setValue("show_stop_warning", False)
                self._execute_stop_procedure()
        else: self._execute_stop_procedure()

    def _execute_stop_procedure(self):
        if self._download_worker: # Changed from self.download_worker to self._download_worker
            self.overall_status_label.setText("Interruzione in corso...")
            self.stop_button.setEnabled(False)
            self._download_worker.request_stop() # Changed from self.download_worker to self._download_worker

    def _update_series_status(self, series_name, status_message):
        status_lower = status_message.lower()
        priority, color = 1, QColor(Qt.GlobalColor.transparent)
        if "download" in status_lower: priority, color = 0, QColor(200, 255, 200)
        elif "conversione" in status_lower: priority, color = 0, QColor(200, 220, 255)
        elif "interrotto" in status_lower: priority, color = 1, QColor(255, 200, 200)
        
        for row in range(self.table_widget.rowCount()):
            if self.table_widget.item(row, 0).text() == series_name:
                status_item = StatusTableWidgetItem(status_message, priority)
                status_item.setTextAlignment(Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignVCenter)
                self.table_widget.setItem(row, 1, status_item)
                self.table_widget.item(row, 1).setBackground(color)
                break
        
        self.table_widget.sortItems(1, Qt.SortOrder.AscendingOrder)

    def _handle_worker_error(self, series_name, error_message):
        if series_name in ["GLOBAL", "DEPENDENCIES", "CONFIG"]:
            QMessageBox.critical(self, f"Errore {series_name}", error_message)
            self._execute_stop_procedure()
        else: self._update_series_status(series_name, f"❌ Errore")
        self.log_output.append(f"ERRORE [{series_name}]: {error_message}")

    def _handle_series_finished(self, series_name, episode_path, download_time, conversion_time):
        self.log_output.append(f"✅ {os.path.basename(episode_path)} | DL: {download_time:.2f}s | Conv: {conversion_time:.2f}s")
        self._update_series_status(series_name, "✅ Fatto")

    def _handle_task_skipped(self, series_name, reason):
        self.log_output.append(f"🚫 SKIPPED [{series_name}]: {reason}")
        self._update_series_status(series_name, f"🚫 Saltato: {reason}")

    def _set_download_in_progress_ui_state(self, in_progress: bool):
        """Imposta lo stato degli elementi UI in base allo stato del download."""
        self.table_widget.setSortingEnabled(not in_progress)
        self.main_splitter.setSizes([450, 200 if in_progress else 0])
        
        if in_progress:
            for row in range(self.table_widget.rowCount()):
                self.table_widget.setItem(row, 1, StatusTableWidgetItem("In coda...", 2))
                self.table_widget.item(row, 1).setBackground(QColor(Qt.GlobalColor.transparent))

        self.start_button.setEnabled(not in_progress)
        self.stop_button.setEnabled(in_progress)
        self.refresh_button.setEnabled(not in_progress)
        self.json_path_input.setEnabled(not in_progress)
        self.output_dir_input.setEnabled(not in_progress)

    def _download_finished(self):
        self._set_download_in_progress_ui_state(False)
        if "Interruzione" not in self.overall_status_label.text():
             self.overall_status_label.setText("Processo completato.")
        
        if self._download_thread:
            self._download_thread.quit()
            self._download_thread.wait()
        self._download_thread = None
        self._download_worker = None
