import os
from pathlib import Path
from PyQt6.QtWidgets import (
    QMainWindow, QVBoxLayout, QHBoxLayout, QWidget,
    QPushButton, QTableWidget, QHeaderView, QFileDialog, QLabel,
    QLineEdit, QMessageBox, QTextEdit, QStyle, QMenuBar, QSplitter, QTableWidgetItem, QApplication, QCheckBox
)
from PyQt6.QtCore import QThread, Qt, QSettings, QByteArray, QEvent, QTimer, QSize
from PyQt6.QtGui import QIcon, QFont, QColor, QAction
from core.download_worker import DownloadWorker
from anidownloader_core.series_repository import SeriesRepository
from anidownloader_config.app_config_manager import AppConfigManager
from anidownloader_config.defaults import DEFAULT_CONFIG_DIR, DEFAULT_SERIES_JSON_PATH, DEFAULT_NUM_CHUNKS
from utils.image_loader import load_poster_image
from .widgets import StatusTableWidgetItem, StopConfirmationDialog
from .settings import SettingsDialog
from .series_manager import SeriesManagerDialog
from .styles import DARK_THEME_QSS, LIGHT_THEME_QSS

class AniDownloaderGUI(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("AniDownloader GUI")
        
        self._is_dark_theme = None # Initialize as None to force first application
        
        # Debounce timer for theme changes
        self._theme_debounce_timer = QTimer()
        self._theme_debounce_timer.setSingleShot(True)
        self._theme_debounce_timer.timeout.connect(self._apply_theme_on_event)
        
        # Apply Theme based on system
        self._apply_theme()
            
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

        self._load_config_paths()
        
        self.series_repository = SeriesRepository(self.json_file_path)
        self._check_series_file()

        self._download_thread, self._download_worker = None, None
        self._series_data = []
        self._init_ui()
        self._load_series_data_into_table()
        self.restore_geometry_and_state()

    def changeEvent(self, event):
        """Handle system events, specifically theme changes."""
        if event.type() == QEvent.Type.PaletteChange:
            app = QApplication.instance()
            if app:
                # Basic check to see if we should even bother starting the timer
                is_dark = app.palette().window().color().lightness() < 128
                if is_dark != self._is_dark_theme:
                    # Debounce to avoid rapid fire events and crashes
                    self._theme_debounce_timer.start(500)
        super().changeEvent(event)

    def _apply_theme_on_event(self):
        """Safely apply theme triggered by an event while preserving selection."""
        old_is_dark = self._is_dark_theme
        
        # Save current selection
        self._preserved_series_name = None
        current_row_index = self.table_widget.currentRow()
        if current_row_index != -1:
            item = self.table_widget.item(current_row_index, 0)
            if item:
                self._preserved_series_name = item.text()

        self._apply_theme()
        
        if old_is_dark != self._is_dark_theme:
            # Find the index of the previously selected item after repopulation
            restored_row_index = -1
            if self._preserved_series_name:
                for r_idx, s_data in enumerate(self._series_data):
                    if s_data.get("name") == self._preserved_series_name:
                        restored_row_index = r_idx
                        break
            
            # Re-populate table, passing the intended selection.
            # _populate_table_main_gui will handle selecting the row and calling _on_series_selected.
            self._load_series_data_into_table(row_to_select=restored_row_index)

    def _apply_theme(self):
        """Detects system theme brightness and applies appropriate stylesheet."""
        app = QApplication.instance()
        if not app: return

        # Simple heuristic: check window background lightness
        palette = app.palette()
        bg_color = palette.window().color()
        lightness = bg_color.lightness()
        
        new_is_dark = lightness < 128
        
        # Guard: Avoid redundant stylesheet applications (expensive)
        if self._is_dark_theme == new_is_dark:
            return
            
        if new_is_dark:
            app.setStyleSheet(DARK_THEME_QSS)
        else:
            app.setStyleSheet(LIGHT_THEME_QSS)
            
        self._is_dark_theme = new_is_dark
        
        # Force table to not show the "focus rect"
        if hasattr(self, 'table_widget'):
            self.table_widget.setFocusPolicy(Qt.FocusPolicy.NoFocus)

    def _load_config_paths(self):
        self.json_file_path = Path(self.app_config_manager.get("json_file_path", str(DEFAULT_SERIES_JSON_PATH)))
        self.output_dir = Path(self.app_config_manager.get("output_dir"))
        self.log_file_path = Path(self.app_config_manager.get("log_file_path"))

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
        self._create_main_layout()
        self._create_control_buttons()
        self._create_series_table()
        self._create_log_output()
        self._setup_main_splitter()
        self._create_overall_status_label()

    def _create_main_layout(self):
        central_widget = QWidget(); self.setCentralWidget(central_widget); self.main_layout = QVBoxLayout(central_widget)
        self.top_container = QWidget(); self.top_layout = QVBoxLayout(self.top_container); self.top_layout.setContentsMargins(0,0,0,0)

    def _create_control_buttons(self):
        button_layout = QHBoxLayout()
        button_layout.setSpacing(12)
        
        # Start Button
        self.start_button = QPushButton("Avvia Download")
        self.start_button.setObjectName("primaryButton") # Apply primary style
        self.start_button.clicked.connect(self.start_download)
        self.start_button.setCursor(Qt.CursorShape.PointingHandCursor)
        self.start_button.setFixedSize(160, 45)
        self.start_button.setFont(QFont("Segoe UI", 10, QFont.Weight.Bold))
        
        # Stop Button
        self.stop_button = QPushButton("Ferma Download")
        self.stop_button.setObjectName("dangerButton") # Apply danger style
        self.stop_button.clicked.connect(self.stop_download)
        self.stop_button.setEnabled(False)
        self.stop_button.setCursor(Qt.CursorShape.PointingHandCursor)
        self.stop_button.setFixedSize(160, 45)
        self.stop_button.setFont(QFont("Segoe UI", 10, QFont.Weight.Bold))
        
        # Utility Buttons
        self.refresh_button = QPushButton("Aggiorna Serie")
        self.refresh_button.clicked.connect(self._load_series_data_into_table)
        self.refresh_button.setMinimumSize(140, 45) # Use setMinimumSize instead of setFixedSize
        self.refresh_button.setCursor(Qt.CursorShape.PointingHandCursor)
        self.refresh_button.setIcon(self.style().standardIcon(QStyle.StandardPixmap.SP_BrowserReload))
        self.refresh_button.setIconSize(QSize(24, 24)) # Standardize icon size
        self.refresh_button.setToolTip("Ricarica i dati delle serie dalla sorgente")
        
        self.manage_series_button = QPushButton("Gestisci Serie")
        self.manage_series_button.clicked.connect(self._open_series_manager)
        self.manage_series_button.setMinimumSize(140, 45) # Use setMinimumSize instead of setFixedSize
        self.manage_series_button.setCursor(Qt.CursorShape.PointingHandCursor)
        self.manage_series_button.setIcon(self.style().standardIcon(QStyle.StandardPixmap.SP_FileDialogDetailedView)) # Using a view-related icon
        self.manage_series_button.setIconSize(QSize(24, 24)) # Standardize icon size
        self.manage_series_button.setToolTip("Aggiungi, modifica o rimuovi le serie")
        
        self.reset_sort_button = QPushButton("Reset Ordine")
        self.reset_sort_button.clicked.connect(self._reset_table_sort)
        self.reset_sort_button.setMinimumSize(140, 45) # Use setMinimumSize instead of setFixedSize
        self.reset_sort_button.setCursor(Qt.CursorShape.PointingHandCursor)
        self.reset_sort_button.setIcon(self.style().standardIcon(QStyle.StandardPixmap.SP_DialogResetButton))
        self.reset_sort_button.setIconSize(QSize(24, 24)) # Standardize icon size
        self.reset_sort_button.setToolTip("Ripristina l'ordinamento predefinito delle serie")
        
        # Settings Button with Icon Fallback
        self.settings_button = QPushButton()
        self.settings_button.setToolTip("Impostazioni")
        self.settings_button.setFixedSize(45, 45)
        self.settings_button.setCursor(Qt.CursorShape.PointingHandCursor)
        
        # Try to load system icon for settings/preferences
        icon = QIcon.fromTheme("preferences-system") # Primary attempt to load a system theme icon
        if icon.isNull():
             icon = QIcon.fromTheme("emblem-system") # Fallback 1: a more generic system emblem
        
        if not icon.isNull():
            self.settings_button.setIcon(icon)
            self.settings_button.setIconSize(QSize(24, 24)) # Standardize icon size
        else:
            # Final Fallback: if no suitable theme icon is found, use a text representation
            self.settings_button.setText("⚙️")
            self.settings_button.setFont(QFont("Segoe UI", 16))

        self.settings_button.clicked.connect(self._open_settings)

        button_layout.addWidget(self.start_button)
        button_layout.addWidget(self.stop_button)
        button_layout.addStretch(1) 
        button_layout.addWidget(self.refresh_button)
        button_layout.addWidget(self.manage_series_button)
        button_layout.addWidget(self.reset_sort_button)
        button_layout.addSpacing(10)
        button_layout.addWidget(self.settings_button)
        
        self.top_layout.addLayout(button_layout)
        self.top_layout.addSpacing(15)

    def _create_series_table(self):
        self.table_widget = QTableWidget()
        self.table_widget.setColumnCount(2)
        self.table_widget.setHorizontalHeaderLabels(["Nome Serie", "Stato"])
        self.table_widget.verticalHeader().setVisible(False) # Hide row numbers
        self.table_widget.setAlternatingRowColors(True) # Better readability
        
        header = self.table_widget.horizontalHeader()
        header.setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        header.setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        header.setMinimumSectionSize(200)
        
        self.table_widget.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.table_widget.setSortingEnabled(True)
        self.table_widget.itemSelectionChanged.connect(self._on_series_selected)
        
        series_display_layout = QHBoxLayout()
        series_display_layout.addWidget(self.table_widget)
        
        self.image_label = QLabel()
        self.image_label.setFixedSize(220, 320) # Slightly larger
        self.image_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.image_label.setObjectName("imageLabel") # ID for styling if needed, but border is inline
        # Keep border inline for simplicity as it relies on specific colors that might change
        # Or better: let QSS handle it if we can target it. 
        # I'll update it dynamically in _apply_theme logic ideally, but for now inline is safe.
        # However, to support light theme, I should use generic QSS or QPalette colors.
        # Let's use a generic border style here and let QSS override via ID if I add it to QSS.
        # For now, I'll stick to a neutral border.
        self.image_label.setStyleSheet("border: 2px solid gray; border-radius: 6px;")
        
        series_display_layout.addWidget(self.image_label)
        self.top_layout.addLayout(series_display_layout)

    def _create_log_output(self):
        self.log_output = QTextEdit()
        self.log_output.setReadOnly(True)

    def _setup_main_splitter(self):
        self.main_splitter = QSplitter(Qt.Orientation.Vertical)
        self.main_splitter.addWidget(self.top_container)
        self.main_splitter.addWidget(self.log_output)
        self.main_splitter.setStretchFactor(0, 3) # Give more space to top
        self.main_splitter.setStretchFactor(1, 1)
        self.main_layout.addWidget(self.main_splitter)

    def _create_overall_status_label(self):
        self.overall_status_label = QLabel("Pronto.")
        self.overall_status_label.setFont(QFont("Segoe UI", 10, QFont.Weight.Bold))
        # Remove hardcoded color to let theme handle it
        self.overall_status_label.setStyleSheet("margin-top: 5px;")
        self.main_layout.addWidget(self.overall_status_label)
        
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

    def _open_settings(self):
        dialog = SettingsDialog(self.app_config_manager, self.settings, parent=self)
        if dialog.exec():
            # Se i percorsi sono cambiati, ricarichiamo tutto
            if dialog.paths_changed:
                self._load_config_paths()
                self.series_repository = SeriesRepository(self.json_file_path)
                self._load_series_data_into_table()
                self.overall_status_label.setText("Impostazioni aggiornate.")

    def _open_series_manager(self):
        dialog = SeriesManagerDialog(series_repository=self.series_repository, parent=self); 
        if dialog.exec(): self._load_series_data_into_table()

    def _load_series_data_into_table(self, row_to_select=0):
        try: self._series_data = self.series_repository.load_series_data()
        except Exception as e: QMessageBox.critical(self, "Errore Caricamento Serie", f"Impossibile caricare: {e}"); self._series_data = []
        self._populate_table_main_gui(self._series_data, row_to_select=row_to_select)
        # Il reset dell'ordinamento è gestito da _reset_table_sort

    def _populate_table_main_gui(self, data_to_display, row_to_select=0):
        self.table_widget.setSortingEnabled(False) # Disabilita l'ordinamento durante il popolamento
        self.table_widget.setRowCount(0); self.table_widget.setRowCount(len(data_to_display))
        for row, series in enumerate(data_to_display):
            name_item = QTableWidgetItem(series["name"]); status_item = StatusTableWidgetItem("In attesa", 3)
            self.table_widget.setItem(row, 0, name_item); self.table_widget.setItem(row, 1, status_item)
        
        if data_to_display:
            if row_to_select != -1:
                # Ensure row_to_select is valid
                row_to_select = min(max(0, row_to_select), len(data_to_display)-1)
                self.table_widget.setCurrentCell(row_to_select, 0)
                self.table_widget.scrollToItem(self.table_widget.item(row_to_select, 0))
            else:
                # If no specific row to select, select the first row
                self.table_widget.setCurrentCell(0, 0)
            # Always call _on_series_selected after (re)populating and selecting a row
            self._on_series_selected() 
        else:
            # If no data, clear selection and image
            self.table_widget.clearSelection()
            self._on_series_selected()
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
        
        # Ricarichiamo sempre la config prima di partire per essere sicuri
        convert_to_h265 = self.app_config_manager.get("convert_to_h265", True)
        num_chunks = self.app_config_manager.get("num_chunks", DEFAULT_NUM_CHUNKS)
        
        # Usiamo i percorsi aggiornati
        self._load_config_paths()

        self._download_thread = QThread()
        self._download_worker = DownloadWorker(
            series_list=self._series_data, 
            json_file_path=self.json_file_path, 
            log_file_path=self.log_file_path, 
            output_dir=self.output_dir, 
            convert_to_h265=convert_to_h265,
            num_chunks=num_chunks
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

        # Determine colors based on theme
        is_dark = getattr(self, '_is_dark_theme', True) # Default to dark if attribute missing
        
        if is_dark:
            # Dark Theme Colors
            if "download" in status_lower: new_priority, color = 0, QColor("#1b5e20") # Dark Green
            elif "conversione" in status_lower: new_priority, color = 0, QColor("#0d47a1") # Dark Blue
            elif "fatto" in status_lower: new_priority, color = 2, QColor("#1b5e20") # Dark Green
            elif "saltato" in status_lower: new_priority, color = 3, QColor(Qt.GlobalColor.transparent)
            elif "errore" in status_lower: new_priority, color = 1, QColor("#b71c1c") # Dark Red
            elif "interrotto" in status_lower: new_priority, color = 1, QColor("#b71c1c") # Dark Red
        else:
            # Light Theme Colors (Lighter pastels)
            if "download" in status_lower: new_priority, color = 0, QColor("#c8e6c9") # Light Green
            elif "conversione" in status_lower: new_priority, color = 0, QColor("#bbdefb") # Light Blue
            elif "fatto" in status_lower: new_priority, color = 2, QColor("#c8e6c9") # Light Green
            elif "saltato" in status_lower: new_priority, color = 3, QColor(Qt.GlobalColor.transparent)
            elif "errore" in status_lower: new_priority, color = 1, QColor("#ffcdd2") # Light Red
            elif "interrotto" in status_lower: new_priority, color = 1, QColor("#ffcdd2") # Light Red
        
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
        self.reset_sort_button.setEnabled(not in_progress)
        self.settings_button.setEnabled(not in_progress)

    def _on_download_finished(self):
        self._set_ui_state_for_download(False)
        if "Interruzione" not in self.overall_status_label.text():
             self.overall_status_label.setText("Processo completato.")
        if self._download_thread:
            self._download_thread.quit(); self._download_thread.wait()
        self._download_thread, self._download_worker = None, None