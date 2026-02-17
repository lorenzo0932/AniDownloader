import os
import re
import subprocess
import sys
import shutil
import time
import logging
import glob
from pathlib import Path

# --- CONFIGURAZIONE SPLITTER ---
RAM_THRESHOLD_PERCENT = 50 
# -------------------------------

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

def get_video_duration(file_path):
    cmd = ['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'default=noprint_wrappers=1:nokey=1', str(file_path)]
    try:
        creationflags = subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0
        result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, creationflags=creationflags)
        return float(result.stdout.strip())
    except Exception:
        return None

def get_ram_usage_percent():
    try:
        if sys.platform != 'linux': return 100.0 
        with open('/proc/meminfo', 'r') as f:
            lines = f.readlines()
        mem_info = {l.split(':')[0]: int(l.split()[1]) for l in lines}
        total = mem_info.get('MemTotal', 1)
        available = mem_info.get('MemAvailable', mem_info.get('MemFree', 0))
        return ((total - available) / total) * 100
    except:
        return 100.0

def parse_progress_file(progress_file):
    last_time = 0
    try:
        if not os.path.exists(progress_file): return 0
        with open(progress_file, 'r') as f:
            lines = f.readlines()
            for line in reversed(lines):
                if line.startswith('out_time_us='):
                    try:
                        last_time = int(line.split('=')[1])
                        break
                    except: pass
    except: pass
    return last_time

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

def convert_and_verify_episode(file_path: str, name: str, output_dir: Path, status_updater, stop_event, log_file_path: Path, max_retries=3, num_chunks=1):
    output_dir_path = Path(output_dir)
    input_file_path = Path(file_path)
    output_dir_path.mkdir(parents=True, exist_ok=True)
    output_path = output_dir_path / input_file_path.name
    
    if output_path.exists(): output_path.unlink()

    for attempt in range(1, max_retries + 1):
        if stop_event.is_set(): raise Exception("Conversione interrotta.")
        
        status_updater.update_progress(name, f"Preparazione - tentativo {attempt}")
        start_time = time.time()
        
        # Selezione Storage
        ram_usage = get_ram_usage_percent()
        if ram_usage < RAM_THRESHOLD_PERCENT and sys.platform.startswith('linux'):
            base_work_root = Path("/dev/shm") / f"splitter_{os.getpid()}_{attempt}"
            storage_mode = "RAM"
        else:
            base_work_root = Path("/var/tmp") if os.path.exists("/var/tmp") else Path(os.getenv('TEMP', '/tmp'))
            base_work_root = base_work_root / f"splitter_{os.getpid()}_{attempt}"
            storage_mode = "DISK"
        
        current_work_dir = base_work_root / str(time.time_ns())
        current_work_dir.mkdir(parents=True, exist_ok=True)
        intermediate_temp_output = current_work_dir / "merged.mp4"
        
        try:
            # 1. Analisi Durata
            total_duration_sec = get_video_duration(input_file_path)
            if not total_duration_sec: raise Exception("Durata non rilevata.")
            total_duration_us = total_duration_sec * 1_000_000
            
            # --- MODIFICA FONDAMENTALE: SEGMENT MUXER ---
            # Invece di calcolare start/end, dividiamo fisicamente il file originale
            # usando -c copy (istantaneo). Questo garantisce tagli perfetti sui Keyframe.
            
            target_segment_time = total_duration_sec / num_chunks
            status_updater.update_progress(name, f"Segmentazione Smart ({storage_mode})...")
            
            # Pattern per i file sorgente splittati: src_000.mp4, src_001.mp4...
            segment_pattern = current_work_dir / "src_%03d.mp4"
            
            cmd_split = [
                "ffmpeg", "-y", "-i", str(input_file_path),
                "-c", "copy", 
                "-map", "0",
                "-f", "segment",
                "-segment_time", str(target_segment_time),
                "-reset_timestamps", "1", # Importante: resetta timestamp per encoding pulito
                str(segment_pattern)
            ]
            
            subprocess.run(cmd_split, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
            
            # Troviamo i file generati (potrebbero essere 4, ma anche 3 o 5 se i keyframe sono strani)
            source_chunks = sorted(glob.glob(str(current_work_dir / "src_*.mp4")))
            
            if not source_chunks:
                raise Exception("Segmentazione fallita: nessun file creato.")

            # 2. Encoding Parallelo dei Chunk
            chunk_processes = []
            chunk_out_files = []
            progress_files = []
            
            status_updater.update_progress(name, f"Encoding {len(source_chunks)} parti...")

            for i, src_chunk in enumerate(source_chunks):
                out_chunk = current_work_dir / f"encoded_{i}.mp4"
                prog_file = current_work_dir / f"prog_{i}.txt"
                
                chunk_out_files.append(out_chunk)
                progress_files.append(prog_file)
                
                # Parametri Balanced per batch safety
                # Nota: Non serve più -ss o -t perché il file in input è già tagliato
                cmd_enc = [
                    "ffmpeg", "-y",
                    "-i", src_chunk,
                    "-c:v", "libx265", "-crf", "23", "-preset", "veryfast",
                    "-threads", "8", 
                    "-x265-params", "hist-scenecut=1",
                    "-c:a", "copy",
                    "-progress", str(prog_file),
                    str(out_chunk)
                ]
                
                creationflags = subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0
                p = subprocess.Popen(cmd_enc, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=creationflags)
                chunk_processes.append(p)
            
            # Monitoraggio
            while True:
                if stop_event.is_set():
                    for p in chunk_processes: p.kill()
                    raise Exception("Interrotto.")
                
                all_done = all(p.poll() is not None for p in chunk_processes)
                
                current_total_us = 0
                for pf in progress_files:
                    current_total_us += parse_progress_file(pf)
                
                # Calcolo percentuale
                percent = min(99, int((current_total_us / total_duration_us) * 100))
                status_updater.update_progress(name, f"Conversione {storage_mode} - {percent}%")
                
                if all_done: break
                time.sleep(0.5)

            if any(p.returncode != 0 for p in chunk_processes):
                raise Exception("Errore in un chunk di encoding.")

            # 3. Unione
            status_updater.update_progress(name, "Unione...")
            list_file = current_work_dir / "list.txt"
            with open(list_file, "w") as f:
                for cf in chunk_out_files:
                    f.write(f"file '{str(cf).replace("'", "'\\''")}'\n")
            
            subprocess.run(["ffmpeg", "-y", "-f", "concat", "-safe", "0", "-i", str(list_file), "-c", "copy", str(intermediate_temp_output)], 
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)

            # 4. Verifica Finale
            status_updater.update_progress(name, "Verifica...")
            log_file = output_path.with_suffix(output_path.suffix + ".log")
            
            # Usiamo -c copy per verifica strutturale veloce (non decodifica tutto)
            # Se vuoi la verifica lenta ma sicura, togli "-c", "copy"
            with open(log_file, "w") as log_f:
                subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", str(intermediate_temp_output), "-c", "copy", "-f", "null", "-"], stderr=log_f)
            
            if log_file.stat().st_size == 0:
                log_file.unlink()
                shutil.move(str(intermediate_temp_output), str(output_path))
                input_file_path.unlink()
                shutil.move(str(output_path), str(input_file_path))
                shutil.rmtree(base_work_root, ignore_errors=True)
                return True, time.time() - start_time
            else:
                shutil.rmtree(base_work_root, ignore_errors=True)
                raise Exception("Verifica fallita (file corrotto).")

        except Exception as e:
            if 'base_work_root' in locals() and base_work_root.exists():
                shutil.rmtree(base_work_root, ignore_errors=True)
            if stop_event.is_set(): raise
            _log_critical_error(log_file_path, f"{name}: {e}")
            if attempt < max_retries: time.sleep(2); continue
            
    raise Exception("Fallito dopo tentativi.")

def process_series_task(task: dict, output_dir: Path, log_file_path: Path, status_updater, stop_event, convert_to_h265: bool, num_chunks: int = 1):
    name = task["series"]["name"]
    episode_path, download_time, conversion_time = None, 0.0, 0.0

    try:
        episode_path, download_time = download_episode(task, status_updater, stop_event, log_file_path)
        
        if convert_to_h265:
            _, conversion_time = convert_and_verify_episode(episode_path, name, output_dir, status_updater, stop_event, log_file_path, num_chunks=num_chunks)
        
        if hasattr(status_updater, 'report_finished'):
            final_filepath = Path(task["series"]["path"]) / task["final_filename"]
            status_updater.report_finished(name, str(final_filepath), download_time, conversion_time)
        
        return {"name": name, "episode": episode_path, "download_time": download_time, "conversion_time": conversion_time, "error": None}

    except Exception as e:
        if not stop_event.is_set(): status_updater.report_error(name, str(e))
        _log_critical_error(log_file_path, f"{name}: {str(e)}")
        return {"name": name, "episode": episode_path, "download_time": download_time, "conversion_time": conversion_time, "error": str(e)}