import os
import re
import multiprocessing as mp
import shutil
from pathlib import Path
from queue import Empty
from PyQt6.QtCore import QObject, pyqtSignal, QTimer

from anidownloader_core.planning_service import plan_single_series
from anidownloader_core.media_processor import process_series_task

try:
    import psutil
except ImportError:
    psutil = None

class DownloadSignals(QObject):
    progress = pyqtSignal(str, str)
    error = pyqtSignal(str, str)
    finished = pyqtSignal(str, str, float, float)
    task_skipped = pyqtSignal(str, str)
    overall_status = pyqtSignal(str)

class DownloadWorker(QObject):
    def __init__(self, series_list, json_file_path: Path, log_file_path: Path, output_dir: Path, convert_to_h265: bool):
        super().__init__()
        self._json_file_path = json_file_path
        self._log_file_path = log_file_path
        self._output_dir = output_dir
        self._convert_to_h265 = convert_to_h265
        self._signals = DownloadSignals()
        self._is_running = True
        self._pool = self._manager = self._queue = self._stop_event = self._timer = None
        self._active_tasks = []
        self._active_tasks_info = []
        self._series_list = series_list
        self._state = "idle"

    def _construct_final_filename(self, task):
        series = task["series"]; download_url = task["download_url"]; final_ep_number = task["final_ep_number"]
        url_filename = download_url.split("/")[-1].split("?")[0]
        filename_root = series.get("filename_root")
        if not filename_root:
            match = re.match(r'(.*?)_Ep_', url_filename, re.IGNORECASE)
            filename_root = match.group(1) if match else os.path.splitext(url_filename)[0]
        suffix_match = re.search(r'(_Ep_.*)', url_filename, re.IGNORECASE)
        if suffix_match:
            suffix = suffix_match.group(1)
            correct_suffix = re.sub(r'(\d+)', f'{final_ep_number:02d}', suffix, 1)
            return filename_root + correct_suffix
        return f"{filename_root}_Ep_{final_ep_number:02d}.mp4"

    def request_stop(self):
        self._is_running = False
        if self._stop_event: self._stop_event.set()

    def _safe_shutdown(self):
        if self._timer: self._timer.stop()
        for task_info in self._active_tasks_info:
            self._signals.progress.emit(task_info['name'], "❌ Interrotto")
        self._signals.overall_status.emit("Interruzione forzata dei processi...")
        if self._pool:
            self._pool.terminate(); self._pool.join()
        if psutil:
            for proc in psutil.process_iter(['name']):
                try:
                    if proc.name().lower() in ["ffmpeg", "aria2c", "ffmpeg.exe", "aria2c.exe"]:
                        proc.kill()
                except (psutil.NoSuchProcess, psutil.AccessDenied): pass
        self._cleanup_temp_files()
        self._signals.overall_status.emit("Interruzione completata.")
        if self.thread(): self.thread().quit()

    def _cleanup_temp_files(self):
        self._signals.overall_status.emit("Pulizia file temporanei...")
        for task_info in self._active_tasks_info:
            try:
                series_path = Path(task_info["path"])
                final_filename = task_info["final_filename"]
                for file_to_remove in [
                    series_path / final_filename, series_path / f"{final_filename}.aria2c",
                    self._output_dir / final_filename, self._output_dir / f"{final_filename}.log"
                ]:
                    if file_to_remove.exists():
                        os.remove(file_to_remove)
                        self._signals.overall_status.emit(f" - Rimosso: {file_to_remove.name}")
            except Exception as e:
                self._signals.error.emit("Cleanup", f"Errore pulizia: {e}")

    def _check_status(self):
        if not self._is_running:
            self._safe_shutdown(); return

        if self._state == "planning":
            if self._active_tasks.ready():
                planned_tasks = self._active_tasks.get()
                self._pool.close(); self._pool.join() # Pulisci il pool di pianificazione
                self._start_downloading(planned_tasks)
            return # Non fare altro mentre pianifichi
        
        if self._state == "downloading":
            try:
                while True: # Legge tutti i messaggi accumulati dalla coda
                    msg = self._queue.get_nowait()
                    signal_type, *args = msg
                    if signal_type == 'progress': self._signals.progress.emit(*args)
                    elif signal_type == 'error': self._signals.error.emit(*args)
                    elif signal_type == 'finished': self._signals.finished.emit(*args)
            except Empty: pass

            if self._active_tasks.ready():
                if self._timer: self._timer.stop()
                if self._pool: self._pool.close(); self._pool.join()
                if self._is_running: self._signals.overall_status.emit("Processo completato.")
                if self.thread(): self.thread().quit()

    def run(self):
        if not self._check_dependencies():
            if self.thread(): self.thread().quit(); return
        
        self._start_planning()
        
    def _start_planning(self):
        self._state = "planning"
        self._signals.overall_status.emit("Pianificazione attività...")
        self._pool = mp.Pool(processes=mp.cpu_count())
        self._active_tasks = self._pool.map_async(plan_single_series, self._series_list)
        
        self._timer = QTimer()
        self._timer.timeout.connect(self._check_status)
        self._timer.start(250)

    def _start_downloading(self, planned_tasks):
        self._state = "downloading"
        if not self._is_running:
            if self.thread(): self.thread().quit(); return

        to_process = [t for t in planned_tasks if t["action"] == "process"]
        for t in [t for t in planned_tasks if t["action"] == "skip"]:
            self._signals.task_skipped.emit(t['series']['name'], t['reason'])
            
        if not to_process:
            self._signals.overall_status.emit("✅ Nessun nuovo episodio da scaricare."); self.thread().quit(); return
        
        self._signals.overall_status.emit(f"Avvio di {len(to_process)} download...")
        for task in to_process:
            task["final_filename"] = self._construct_final_filename(task)
        self._active_tasks_info = [{"name": t["series"]["name"], "path": t["series"]["path"], "final_filename": t["final_filename"]} for t in to_process]
        
        self._manager = mp.Manager()
        self._queue = self._manager.Queue()
        self._stop_event = self._manager.Event()
        
        queue_updater = QueueStatusUpdater(self._queue)
        
        self._pool = mp.Pool(processes=mp.cpu_count())
        pool_args = [(task, self._output_dir, self._log_file_path, queue_updater, self._stop_event, self._convert_to_h265) for task in to_process]
        self._active_tasks = self._pool.starmap_async(process_series_task, pool_args)
        # Non chiudere il pool qui, aspetta che i task finiscano in _check_status

    def _check_dependencies(self):
        if not psutil: self._signals.error.emit("DEPENDENCIES", "Manca 'psutil'. Installalo con: pip install psutil"); return False
        missing = [dep for dep in ["aria2c", "ffmpeg"] if not shutil.which(dep)]
        if missing: self._signals.error.emit("DEPENDENCIES", f"Mancanti: {', '.join(missing)}"); return False
        self._signals.overall_status.emit("✅ Dipendenze trovate."); return True

# --- MODIFICA CHIAVE ---
# Aggiunto il metodo mancante per comunicare il successo
class QueueStatusUpdater:
    def __init__(self, queue): self._queue = queue
    def update_progress(self, name: str, msg: str): self._queue.put(('progress', name, msg))
    def report_error(self, name: str, err_msg: str): self._queue.put(('error', name, err_msg))
    def report_finished(self, name: str, path: str, dl_time: float, conv_time: float):
        self._queue.put(('finished', name, path, dl_time, conv_time))
# --- FINE MODIFICA ---