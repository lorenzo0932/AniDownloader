import os
import re
import requests
import subprocess
import sys
import shutil
import time
import multiprocessing as mp
import json
import logging
from pathlib import Path
from queue import Empty
from PyQt6.QtCore import QObject, pyqtSignal, QTimer
from config.defaults import DEFAULT_LOG_FILE, DEFAULT_OUTPUT_DIR, DEFAULT_SERIES_JSON_PATH

try:
    import psutil
except ImportError:
    psutil = None

DEFAULT_JSON_FILE_PATH = DEFAULT_SERIES_JSON_PATH

def get_next_episode_num(series_path):
    if not os.path.exists(series_path): os.makedirs(series_path)
    max_ep = 0
    for filename in os.listdir(series_path):
        if filename.endswith(('.mp4', '.mkv')):
            match = re.search(r'[._-]Ep[._-]?(\d+)', filename, re.IGNORECASE)
            if match: max_ep = max(max_ep, int(match.group(1)))
    return max_ep + 1

def plan_series_task(series):
    path, link_pattern = series["path"], series["link_pattern"]
    is_continuation, passed_episodes = series.get("continue", False), series.get("passed_episodes", 0)
    final_ep_number = get_next_episode_num(path)
    if is_continuation and final_ep_number <= passed_episodes:
        final_ep_number = passed_episodes + 1
    next_ep_download = final_ep_number if not is_continuation else final_ep_number - passed_episodes
    download_url = link_pattern.format(ep=f"{next_ep_download:02d}")
    task = {"series": series, "action": "skip", "reason": "URL non raggiungibile.", "download_url": download_url, "next_ep_download": next_ep_download, "final_ep_number": final_ep_number}
    try:
        response = requests.head(download_url, timeout=10, allow_redirects=True)
        if response.status_code == 200: task.update({"action": "process", "reason": f"Pronto per scaricare Ep. {final_ep_number}"})
        else: task["reason"] = f"HTTP {response.status_code}"
    except requests.RequestException: pass
    return task

def _log_critical_error_mp(log_file_path, message):
    logging.basicConfig(filename=log_file_path, level=logging.ERROR, format='%(asctime)s - %(levelname)s - %(message)s')
    logging.error(message)

def _download_episode_mp(task, queue, stop_event):
    series, name, path, download_url = task["series"], task["series"]["name"], task["series"]["path"], task["download_url"]
    queue.put(('progress', name, f"Download Ep. {task['final_ep_number']}"))
    downloaded_file_name = download_url.split("/")[-1].split("?")[0]
    output_file_path = Path(path) / downloaded_file_name
    cmd = ["aria2c", "-x", "16", "-s", "16", "--summary-interval=1", "-o", str(output_file_path.name), download_url]
    start_time, process = time.time(), subprocess.Popen(cmd, cwd=path, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0)
    try:
        while not stop_event.is_set():
            line = process.stdout.readline()
            if not line: break
            if match := re.search(r'\((\d+)%\)', line): queue.put(('progress', name, f"Download Ep. {task['final_ep_number']} - {match.group(1)}%"))
        if stop_event.is_set(): process.kill(); raise Exception("Download interrotto.")
    except Exception as e: process.kill(); raise Exception(f"Errore stdout: {e}")
    process.wait()
    if process.returncode != 0: raise Exception("aria2c ha fallito.")
    final_file_path = output_file_path
    if series.get("continue", False):
        ep_str_dl, ep_str_final = f"{task['next_ep_download']:02d}", f"{task['final_ep_number']:02d}"
        new_filename = re.sub(f'Ep[._-]?{ep_str_dl}', f'Ep_{ep_str_final}', output_file_path.name, flags=re.IGNORECASE)
        final_file_path = output_file_path.with_name(new_filename)
        os.rename(output_file_path, final_file_path)
    return str(final_file_path), time.time() - start_time

def _convert_and_verify_mp(file_path, name, output_dir, queue, stop_event, max_retries=3):
    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, os.path.basename(file_path))
    for attempt in range(1, max_retries + 1):
        if stop_event.is_set(): raise Exception("Conversione interrotta.")
        queue.put(('progress', name, f"Conversione - tentativo {attempt}"))
        start_time = time.time()
        try:
            cmd = ["nice", "-n", "5", "ffmpeg", "-y", "-i", file_path, "-c:v", "libx265", "-crf", "23", "-preset", "veryfast", "-threads", "12", "-x265-params", "hist-scenecut=1", "-c:a", "copy", output_path]
            proc, total_duration = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0), None
            while not stop_event.is_set():
                line = proc.stdout.readline()
                if not line: break
                if total_duration is None:
                    if match_dur := re.search(r'Duration: (\d+):(\d+):(\d+).(\d+)', line): h, m, s, ms = map(int, match_dur.groups()); total_duration = h * 3600 + m * 60 + s + ms / 100
                if total_duration and (match_time := re.search(r'time=(\d+):(\d+):(\d+).(\d+)', line)): h, m, s, ms = map(int, match_time.groups()); percent = min(100, int(((h * 3600 + m * 60 + s + ms / 100) / total_duration) * 100)); queue.put(('progress', name, f"Conversione - {percent}%"))
            if stop_event.is_set(): proc.kill(); raise Exception("Conversione interrotta.")
            proc.wait()
            log_file = f"{output_path}.log"
            with open(log_file, "w") as log_f: subprocess.run(["nice", "-n", "5", "ffmpeg", "-y", "-v", "error", "-i", output_path, "-f", "null", "-"], stderr=log_f)
            if os.path.getsize(log_file) == 0: os.remove(log_file); os.remove(file_path); shutil.move(output_path, file_path); return True, time.time() - start_time
            else: os.remove(output_path); os.remove(log_file); continue
        except Exception: continue
    raise Exception("Errore conversione dopo vari tentativi.")

def process_series_worker_mp(task, output_dir, log_file_path, queue, stop_event):
    name = task["series"]["name"]
    try:
        episode_path, download_time = _download_episode_mp(task, queue, stop_event)
        result, conversion_time = _convert_and_verify_mp(episode_path, name, output_dir, queue, stop_event)
        if not stop_event.is_set(): queue.put(('finished', name, episode_path, download_time, conversion_time))
    except Exception as e:
        if not stop_event.is_set(): queue.put(('error', name, str(e))); _log_critical_error_mp(log_file_path, f"{name}: {str(e)}")

class DownloadSignals(QObject):
    progress = pyqtSignal(str, str); error = pyqtSignal(str, str); finished = pyqtSignal(str, str, float, float); task_skipped = pyqtSignal(str, str); overall_status = pyqtSignal(str)

class DownloadWorker(QObject):
    def __init__(self, series_list, json_file_path=None, log_file_path=None, output_dir=None):
        super().__init__()
        self._json_file_path = json_file_path or DEFAULT_JSON_FILE_PATH
        self._log_file_path = log_file_path or DEFAULT_LOG_FILE
        self._output_dir = output_dir or DEFAULT_OUTPUT_DIR
        self._signals = DownloadSignals(); self._is_running = True
        self._pool = self._manager = self._queue = self._stop_event = self._timer = None
        self._active_tasks = []; self._active_tasks_info = []

    def request_stop(self):
        self._is_running = False
        if self._stop_event: self._stop_event.set()

    def _safe_shutdown(self):
        if self._timer: self._timer.stop()
        
        for task_info in self._active_tasks_info:
            self._signals.progress.emit(task_info['name'], "❌ Interrotto")

        self._signals.overall_status.emit("Interruzione forzata dei processi...")
        if self._pool:
            self._pool.terminate()
            self._pool.join()
        
        if psutil:
            for proc in psutil.process_iter(['name']):
                try:
                    if proc.name().lower() in ["ffmpeg", "aria2c", "ffmpeg.exe", "aria2c.exe"]:
                        proc.kill()
                        self._signals.overall_status.emit(f" - Ucciso processo: {proc.name()}")
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    pass

        self._cleanup_temp_files()
        self._signals.overall_status.emit("Interruzione completata.")
        if self.thread(): self.thread().quit()

    def _cleanup_temp_files(self):
        self._signals.overall_status.emit("Pulizia file temporanei...")
        for task_info in self._active_tasks_info:
            try:
                series_path = Path(task_info["path"])
                dl_filename = task_info["dl_url"].split("/")[-1].split("?")[0]
                for file_to_remove in [
                    series_path / dl_filename, series_path / f"{dl_filename}.aria2c",
                    Path(self._output_dir) / dl_filename, Path(self._output_dir) / f"{dl_filename}.log"
                ]:
                    if file_to_remove.exists():
                        os.remove(file_to_remove)
                        self._signals.overall_status.emit(f" - Rimosso: {file_to_remove.name}")
            except Exception as e:
                self._signals.error.emit("Cleanup", f"Errore pulizia: {e}")

    def _check_queue(self):
        if not self._is_running:
            self._safe_shutdown()
            return

        try:
            while True:
                msg = self._queue.get_nowait()
                signal_type, *args = msg
                if signal_type == 'progress': self._signals.progress.emit(*args)
                elif signal_type == 'error': self._signals.error.emit(*args)
                elif signal_type == 'finished': self._signals.finished.emit(*args)
        except Empty: pass
            
        if all(r.ready() for r in self._active_tasks):
            if self._timer: self._timer.stop()
            if self._pool: self._pool.join()
            if self._is_running: self._signals.overall_status.emit("Processo completato.")
            if self.thread(): self.thread().quit()

    def run(self):
        if not self._check_dependencies():
            if self.thread(): self.thread().quit(); return
        try: series_list = self._load_series_data()
        except Exception:
            if self.thread(): self.thread().quit(); return

        self._signals.overall_status.emit("Pianificazione attività...")
        try:
            with mp.Pool(processes=mp.cpu_count()) as pool:
                planned_tasks = pool.map(plan_series_task, series_list)
        except Exception as e:
            self._signals.error.emit("GLOBAL", f"Errore pianificazione: {e}")
            if self.thread(): self.thread().quit(); return

        if not self._is_running:
            if self.thread(): self.thread().quit(); return

        to_process = [t for t in planned_tasks if t["action"] == "process"]
        for t in [t for t in planned_tasks if t["action"] == "skip"]:
            self._signals.task_skipped.emit(t['series']['name'], t['reason'])
            
        if not to_process:
            self._signals.overall_status.emit("✅ Nessun nuovo episodio da scaricare.")
            if self.thread(): self.thread().quit(); return
        
        self._signals.overall_status.emit(f"Avvio di {len(to_process)} download...")
        
        self._active_tasks_info = [{"name": t["series"]["name"], "path": t["series"]["path"], "dl_url": t["download_url"]} for t in to_process]

        self._manager = mp.Manager()
        self._queue = self._manager.Queue()
        self._stop_event = self._manager.Event()
        self._pool = mp.Pool(processes=mp.cpu_count())
        self._active_tasks = [self._pool.apply_async(process_series_worker_mp, args=(task, self._output_dir, self._log_file_path, self._queue, self._stop_event)) for task in to_process]
        self._pool.close()
        
        self._timer = QTimer()
        self._timer.timeout.connect(self._check_queue)
        self._timer.start(250)

    def _check_dependencies(self):
        if not psutil:
            self._signals.error.emit("DEPENDENCIES", "Manca 'psutil'. Installalo con: pip install psutil")
            return False
        missing = [dep for dep in ["aria2c", "ffmpeg"] if not shutil.which(dep)]
        if missing:
            self._signals.error.emit("DEPENDENCIES", f"Mancanti: {', '.join(missing)}")
            return False
        self._signals.overall_status.emit("✅ Dipendenze trovate."); return True

    def _load_series_data(self):
        try:
            with open(self._json_file_path, 'r', encoding='utf-8') as f: return json.load(f)
        except Exception as e: self._signals.error.emit("CONFIG", f"Errore caricamento: {e}"); raise

def save_series_data(json_file_path, series_data):
    try:
        with open(json_file_path, 'w', encoding='utf-8') as f: json.dump(series_data, f, indent=4, ensure_ascii=False)
    except Exception as e: raise Exception(f"Errore salvataggio: {e}")
