import os
import re
import subprocess
import sys
import shutil
import time
import logging
from pathlib import Path

def _log_critical_error(log_file_path, message):
    logging.basicConfig(filename=log_file_path, level=logging.ERROR, format='%(asctime)s - %(levelname)s - %(message)s')
    logging.error(message)

def download_episode(task: dict, queue, stop_event, log_file_path: Path):
    """
    Scarica un episodio utilizzando aria2c.
    Invia aggiornamenti di progresso alla coda.
    """
    series, name, path, download_url = task["series"], task["series"]["name"], task["series"]["path"], task["download_url"]
    queue.put(('progress', name, f"Download Ep. {task['final_ep_number']}"))
    
    downloaded_file_name = download_url.split("/")[-1].split("?")[0]
    output_file_path = Path(path) / downloaded_file_name
    
    cmd = ["aria2c", "-x", "16", "-s", "16", "--summary-interval=1", "-o", str(output_file_path.name), download_url]
    
    start_time = time.time()
    process = subprocess.Popen(cmd, cwd=path, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0)
    
    try:
        while not stop_event.is_set():
            line = process.stdout.readline()
            if not line:
                break
            if match := re.search(r'\((\d+)%\)', line):
                queue.put(('progress', name, f"Download Ep. {task['final_ep_number']} - {match.group(1)}%"))
        
        if stop_event.is_set():
            process.kill()
            raise Exception("Download interrotto.")
            
    except Exception as e:
        process.kill()
        _log_critical_error(log_file_path, f"{name}: Errore durante il download: {e}")
        raise Exception(f"Errore durante il download: {e}")
        
    process.wait()
    if process.returncode != 0:
        _log_critical_error(log_file_path, f"{name}: aria2c ha fallito con codice {process.returncode}")
        raise Exception("aria2c ha fallito.")
        
    final_file_path = output_file_path
    if series.get("continue", False):
        ep_str_dl = f"{task['next_ep_download']:02d}"
        ep_str_final = f"{task['final_ep_number']:02d}"
        new_filename = re.sub(f'Ep[._-]?{ep_str_dl}', f'Ep_{ep_str_final}', output_file_path.name, flags=re.IGNORECASE)
        final_file_path = output_file_path.with_name(new_filename)
        os.rename(output_file_path, final_file_path)
        
    return str(final_file_path), time.time() - start_time

def convert_and_verify_episode(file_path: str, name: str, output_dir: Path, queue, stop_event, log_file_path: Path, max_retries=3):
    """
    Converte un episodio utilizzando ffmpeg e verifica l'integrità.
    Invia aggiornamenti di progresso alla coda.
    """
    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, os.path.basename(file_path))
    
    for attempt in range(1, max_retries + 1):
        if stop_event.is_set():
            raise Exception("Conversione interrotta.")
            
        queue.put(('progress', name, f"Conversione - tentativo {attempt}"))
        start_time = time.time()
        
        try:
            cmd = ["nice", "-n", "5", "ffmpeg", "-y", "-i", file_path, "-c:v", "libx265", "-crf", "23", "-preset", "veryfast", "-threads", "12", "-x265-params", "hist-scenecut=1", "-c:a", "copy", output_path]
            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0)
            
            total_duration = None
            while not stop_event.is_set():
                line = proc.stdout.readline()
                if not line:
                    break
                
                if total_duration is None:
                    if match_dur := re.search(r'Duration: (\d+):(\d+):(\d+).(\d+)', line):
                        h, m, s, ms = map(int, match_dur.groups())
                        total_duration = h * 3600 + m * 60 + s + ms / 100
                
                if total_duration and (match_time := re.search(r'time=(\d+):(\d+):(\d+).(\d+)', line)):
                    h, m, s, ms = map(int, match_time.groups())
                    percent = min(100, int(((h * 3600 + m * 60 + s + ms / 100) / total_duration) * 100))
                    queue.put(('progress', name, f"Conversione - {percent}%"))
            
            if stop_event.is_set():
                proc.kill()
                raise Exception("Conversione interrotta.")
                
            proc.wait()
            
            log_file = f"{output_path}.log"
            with open(log_file, "w") as log_f:
                subprocess.run(["nice", "-n", "5", "ffmpeg", "-y", "-v", "error", "-i", output_path, "-f", "null", "-"], stderr=log_f)
            
            if os.path.getsize(log_file) == 0: # Conversione riuscita se il log è vuoto
                os.remove(log_file)
                os.remove(file_path) # Rimuovi il file originale non convertito
                shutil.move(output_path, file_path) # Sposta il file convertito al posto dell'originale
                return True, time.time() - start_time
            else: # Conversione fallita, log non vuoto
                os.remove(output_path)
                os.remove(log_file)
                continue # Riprova
                
        except Exception as e:
            _log_critical_error(log_file_path, f"{name}: Errore durante la conversione (tentativo {attempt}): {e}")
            continue # Riprova
            
    _log_critical_error(log_file_path, f"{name}: Conversione fallita dopo {max_retries} tentativi.")
    raise Exception("Errore conversione dopo vari tentativi.")

def process_series_task(task: dict, output_dir: Path, log_file_path: Path, queue, stop_event):
    """
    Orchestra il download e la conversione di un singolo episodio.
    """
    name = task["series"]["name"]
    try:
        episode_path, download_time = download_episode(task, queue, stop_event, log_file_path)
        result, conversion_time = convert_and_verify_episode(episode_path, name, output_dir, queue, stop_event, log_file_path)
        if not stop_event.is_set():
            queue.put(('finished', name, episode_path, download_time, conversion_time))
    except Exception as e:
        if not stop_event.is_set():
            queue.put(('error', name, str(e)))
        _log_critical_error(log_file_path, f"{name}: {str(e)}")
