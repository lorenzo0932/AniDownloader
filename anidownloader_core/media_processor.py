import os
import re
import subprocess
import sys
import shutil
import time
import logging
from pathlib import Path

def _log_critical_error(log_file_path, message):
    handler = logging.FileHandler(log_file_path)
    formatter = logging.Formatter('%(asctime)s - %(levelname)s - %(message)s')
    handler.setFormatter(formatter)
    logger = logging.getLogger(str(log_file_path))
    logger.setLevel(logging.ERROR)
    if not logger.handlers:
        logger.addHandler(handler)
    logger.error(message)
    handler.close()
    logger.removeHandler(handler)

def download_episode(task: dict, status_updater, stop_event, log_file_path: Path):
    name = task["series"]["name"]
    path = task["series"]["path"]
    download_url = task["download_url"]
    final_ep_number = task["final_ep_number"]
    final_filename = task["final_filename"]
    
    status_updater.update_progress(name, f"Download Ep. {final_ep_number}")
    output_file_path = Path(path) / final_filename
    
    cmd = ["aria2c", "-x", "16", "-s", "16", "--summary-interval=1", "-o", str(output_file_path.name), download_url]
    
    start_time = time.time()
    creationflags = subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0
    process = subprocess.Popen(cmd, cwd=path, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, creationflags=creationflags)
    
    try:
        while not stop_event.is_set():
            line = process.stdout.readline()
            if not line: break
            if match := re.search(r'\((\d+)%\)', line):
                status_updater.update_progress(name, f"Download Ep. {final_ep_number} - {match.group(1)}%")
        
        if stop_event.is_set(): process.kill(); raise Exception("Download interrotto.")
            
    except Exception as e:
        process.kill()
        _log_critical_error(log_file_path, f"{name}: Errore durante il download: {e}")
        raise Exception(f"Errore durante il download: {e}")
        
    process.wait()
    if process.returncode != 0:
        _log_critical_error(log_file_path, f"{name}: aria2c ha fallito con codice {process.returncode}")
        raise Exception("aria2c ha fallito.")
        
    return str(output_file_path), time.time() - start_time

def convert_and_verify_episode(file_path: str, name: str, output_dir: Path, status_updater, stop_event, log_file_path: Path, max_retries=3):
    output_dir_path = Path(output_dir)
    input_file_path = Path(file_path)
    output_dir_path.mkdir(parents=True, exist_ok=True)
    output_path = output_dir_path / input_file_path.name
    
    for attempt in range(1, max_retries + 1):
        if stop_event.is_set(): raise Exception("Conversione interrotta.")
            
        status_updater.update_progress(name, f"Conversione - tentativo {attempt}")
        start_time = time.time()
        
        try:
            cmd = ["ffmpeg", "-y", "-i", str(input_file_path), "-c:v", "libx265", "-crf", "23", "-preset", "veryfast", "-threads", "12", "-x265-params", "hist-scenecut=1", "-c:a", "copy", str(output_path)]
            creationflags = subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0
            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, creationflags=creationflags)
            
            total_duration = None
            while not stop_event.is_set():
                line = proc.stdout.readline()
                if not line: break
                if total_duration is None:
                    if match_dur := re.search(r'Duration: (\d+):(\d+):(\d+).(\d+)', line):
                        h, m, s, ms = map(int, match_dur.groups()); total_duration = h * 3600 + m * 60 + s + ms / 100
                if total_duration and (match_time := re.search(r'time=(\d+):(\d+):(\d+).(\d+)', line)):
                    h, m, s, ms = map(int, match_time.groups()); percent = min(100, int(((h * 3600 + m * 60 + s + ms / 100) / total_duration) * 100)); status_updater.update_progress(name, f"Conversione - {percent}%")
            
            if stop_event.is_set(): proc.kill(); raise Exception("Conversione interrotta.")
                
            proc.wait()
            
            log_file = output_path.with_suffix(output_path.suffix + ".log")
            with open(log_file, "w") as log_f:
                subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", str(output_path), "-f", "null", "-"], stderr=log_f)
            
            if log_file.stat().st_size == 0:
                log_file.unlink()
                input_file_path.unlink()
                shutil.move(str(output_path), str(input_file_path))
                return True, time.time() - start_time
            else:
                output_path.unlink()
                log_file.unlink()
                continue
                
        except Exception as e:
            _log_critical_error(log_file_path, f"{name}: Errore durante la conversione (tentativo {attempt}): {e}")
            continue
            
    _log_critical_error(log_file_path, f"{name}: Conversione fallita dopo {max_retries} tentativi.")
    raise Exception("Errore conversione dopo vari tentativi.")

def process_series_task(task: dict, output_dir: Path, log_file_path: Path, status_updater, stop_event, convert_to_h265: bool):
    name = task["series"]["name"]
    episode_path, download_time, conversion_time = None, 0.0, 0.0

    try:
        episode_path, download_time = download_episode(task, status_updater, stop_event, log_file_path)
        print(f"{episode_path})")
        if convert_to_h265:
            _, conversion_time = convert_and_verify_episode(episode_path, name, output_dir, status_updater, stop_event, log_file_path)
        
        # --- MODIFICA CHIAVE ---
        # Comunica il successo alla GUI, se possibile, senza rompere la CLI.
        # hasattr controlla se l'oggetto 'status_updater' ha il metodo 'report_finished'.
        # Questo sarà vero solo per la GUI, non per la CLI.
        if hasattr(status_updater, 'report_finished'):
            final_filepath = Path(task["series"]["path"]) / task["final_filename"]
            status_updater.report_finished(name, str(final_filepath), download_time, conversion_time)
        # --- FINE MODIFICA ---
        
        return {"name": name, "episode": episode_path, "download_time": download_time, "conversion_time": conversion_time, "error": None}

    except Exception as e:
        if not stop_event.is_set():
            status_updater.report_error(name, str(e))
        _log_critical_error(log_file_path, f"{name}: {str(e)}")
        return {"name": name, "episode": episode_path, "download_time": download_time, "conversion_time": conversion_time, "error": str(e)}