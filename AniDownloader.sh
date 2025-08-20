#! /usr/bin/python3

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
from urllib.parse import urljoin
from bs4 import BeautifulSoup

# Assicurati che questo import sia corretto per la tua struttura di progetto
# Se questo script è standalone, potresti dover definire questi percorsi manualmente
from AniDownloaderGUI.config.defaults import DEFAULT_LOG_FILE, DEFAULT_OUTPUT_DIR, DEFAULT_CONFIG_DIR, DEFAULT_SERIES_JSON_PATH, DEFAULT_APP_CONFIG_PATH

# --- CONFIGURAZIONE ---
JSON_FILE_PATH = DEFAULT_SERIES_JSON_PATH
LOG_FILE = DEFAULT_LOG_FILE

# def check_dependencies():
#     missing_dependencies = []
#     if not shutil.which("aria2c"):
#         missing_dependencies.append("aria2c")
#     if not shutil.which("ffmpeg"):
#         missing_dependencies.append("ffmpeg")

#     if missing_dependencies:
#         print(f"ERRORE: Le seguenti dipendenze non sono state trovate nel PATH: {', '.join(missing_dependencies)}.")
#         print("Questi sono strumenti a riga di comando e non possono essere installati tramite pip.")
#         print("Si prega di installarli manualmente utilizzando il gestore di pacchetti del sistema (es. apt, yum, brew, winget) o scaricando i binari.")
#         print("\nEsempi di installazione:")
#         print("  - Debian/Ubuntu: sudo apt install aria2 ffmpeg")
#         print("  - Fedora: sudo dnf install aria2 ffmpeg")
#         print("  - macOS (Homebrew): brew install aria2 ffmpeg")
#         print("  - Windows (Winget): winget install aria2; winget install ffmpeg")
#         sys.exit(1)
#     print("✅ Dipendenze trovate.")

def load_series_data():
    try:
        with open(JSON_FILE_PATH, 'r', encoding='utf-8') as f:
            return json.load(f)
    except Exception as e:
        print(f"ERRORE: {e}")
        sys.exit(1)

def get_next_episode_num(series_path):
    if not os.path.exists(series_path):
        os.makedirs(series_path)
    max_ep = 0
    for filename in os.listdir(series_path):
        if filename.endswith(('.mp4', '.mkv')):
            match = re.search(r'[._-]Ep[._-]?(\d+)', filename, re.IGNORECASE)
            if match:
                ep_num = int(match.group(1))
                max_ep = max(max_ep, ep_num)
    return max_ep + 1

def plan_series_task(series):
    """
    Pianifica il download di una serie usando un approccio di scraping a 2 fasi.
    Fase 1: Trova il link alla pagina del nuovo episodio dalla lista principale.
    Fase 2: Visita la pagina dell'episodio per trovare il link di download finale.
    """
    name = series["name"]
    path = series["path"]
    series_page_url = series.get("series_page_url")
    episode_list_selector = series.get("episode_list_selector")
    download_link_selector = series.get("download_link_selector")

    if not all([series_page_url, episode_list_selector, download_link_selector]):
        return { "series": series, "action": "skip", "reason": "Configurazione scraping incompleta." }

    task = { "series": series, "action": "skip", "reason": "Nessun nuovo episodio trovato." }

    try:
        # FASE 1: Trova i link a tutti gli episodi
        headers = {'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36'}
        response_main = requests.get(series_page_url, timeout=15, headers=headers)
        response_main.raise_for_status()
        soup_main = BeautifulSoup(response_main.text, 'lxml')
        
        episode_page_links = soup_main.select(episode_list_selector)
        if not episode_page_links:
            task["reason"] = "Selettore lista episodi non ha trovato link."
            return task

        found_episodes = []
        for link in episode_page_links:
            ep_num_str = link.get('data-episode-num') or link.get_text(strip=True)
            match = re.search(r'(\d+)', ep_num_str)
            if match:
                ep_num = int(match.group(1))
                ep_page_url = urljoin(series_page_url, link.get('href'))
                found_episodes.append({"number": ep_num, "page_url": ep_page_url})
        
        if not found_episodes:
            task["reason"] = "Impossibile estrarre i numeri degli episodi dai link."
            return task

        # Logica di Traduzione e Confronto per Serie in Continuazione
        is_continuation = series.get("continue", False)
        passed_episodes = series.get("passed_episodes", 0)
        next_episode_on_disk = get_next_episode_num(path)
        
        found_episodes.sort(key=lambda x: x['number'])
        
        episode_to_process = None
        final_episode_number_for_task = 0

        for ep in found_episodes:
            local_equivalent_number = ep['number']
            if is_continuation:
                local_equivalent_number += passed_episodes

            if local_equivalent_number >= next_episode_on_disk:
                episode_to_process = ep
                final_episode_number_for_task = local_equivalent_number
                break
        
        if not episode_to_process:
            return task

        # FASE 2: Trova il link di download finale
        ep_page_url = episode_to_process['page_url']
        response_ep = requests.get(ep_page_url, timeout=15, headers=headers)
        response_ep.raise_for_status()
        soup_ep = BeautifulSoup(response_ep.text, 'lxml')
        
        final_link_element = soup_ep.select_one(download_link_selector)
        
        if not final_link_element:
            all_links = soup_ep.find_all('a', href=True)
            for link in all_links:
                if "download alternativo" in link.get_text(strip=True).lower():
                    final_link_element = link
                    break
        
        if final_link_element:
            final_download_url = urljoin(ep_page_url, final_link_element.get('href'))
            task.update({
                "action": "process",
                "reason": f"Pronto per scaricare Ep. {final_episode_number_for_task}",
                "download_url": final_download_url,
                "final_ep_number": final_episode_number_for_task
            })
        else:
            task["reason"] = f"Trovato Ep. {final_episode_number_for_task}, ma non il link di download finale."

    except requests.RequestException as e:
        task["reason"] = f"Errore di rete: {e}"
    except Exception as e:
        task["reason"] = f"Errore imprevisto: {e}"

    return task

def log_critical_error(message):
    with open(LOG_FILE, 'a') as log:
        log.write(f"[{time.ctime()}] {message}\n")

def download_episode(task, status_dict):
    series = task["series"]
    name = series["name"]
    path = series["path"]
    download_url = task["download_url"]
    
    status_dict[name] = f"Download Ep. {task['final_ep_number']}"
    
    original_filename = download_url.split("/")[-1].split("?")[0]
    _, extension = os.path.splitext(original_filename)
    if not extension or len(extension) > 5: extension = ".mp4"
    
    clean_name = re.sub(r'[\W_]+', '_', name)
    downloaded_file_name = f"{clean_name}_Ep_{task['final_ep_number']:02d}{extension}"
    output_file_path = Path(path) / downloaded_file_name

    cmd = [
        "aria2c", "-x", "16", "-s", "16",
        "--summary-interval=1", 
        "-o", str(output_file_path.name), download_url
    ]

    start_time = time.time()
    process = subprocess.Popen(
        cmd, cwd=path,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True
    )

    try:
        while True:
            line = process.stdout.readline()
            if not line: break
            match = re.search(r'\((\d+)%\)', line)
            if match:
                percent = match.group(1)
                status_dict[name] = f"Download Ep. {task['final_ep_number']} - {percent}%"
    except Exception as e:
        process.kill()
        raise Exception(f"Errore durante lettura stdout: {e}")

    process.wait()
    end_time = time.time()

    if process.returncode != 0:
        logging.error(f"[{name}] aria2c ha fallito. Codice: {process.returncode}")
        raise Exception("aria2c ha fallito. Controlla il log per dettagli.")

    return str(output_file_path), end_time - start_time

def convert_and_verify(file_path, status_dict, name, max_retries=3):
    output_dir = DEFAULT_OUTPUT_DIR
    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, os.path.basename(file_path))
    log_path = f"{output_path}.log"

    for attempt in range(1, max_retries + 1):
        status_dict[name] = f"Conversione - tentativo {attempt}"
        start_time = time.time()
        try:
            cmd = [
                "nice", "-n", "5", "ffmpeg", "-y", "-i", file_path,
                "-c:v", "libx265", "-crf", "23", "-preset", "veryfast",
                "-threads", "12", "-x265-params", "hist-scenecut=1",
                "-c:a", "copy", output_path
            ]

            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            total_duration = None
            pattern_dur = re.compile(r'Duration: (\d+):(\d+):(\d+).(\d+)')
            pattern_time = re.compile(r'time=(\d+):(\d+):(\d+).(\d+)')

            while True:
                line = proc.stdout.readline()
                if not line:
                    break

                if total_duration is None:
                    match_dur = pattern_dur.search(line)
                    if match_dur:
                        h, m, s, ms = map(int, match_dur.groups())
                        total_duration = h * 3600 + m * 60 + s + ms / 100

                match_time = pattern_time.search(line)
                if match_time and total_duration:
                    h, m, s, ms = map(int, match_time.groups())
                    current_time = h * 3600 + m * 60 + s + ms / 100
                    percent = min(100, int((current_time / total_duration) * 100))
                    status_dict[name] = f"Conversione - {percent}%"

            proc.wait()

            verify = subprocess.run([
                "nice", "-n", "5", "ffmpeg", "-y", "-v", "error", "-i",
                output_path, "-f", "null", "-"
            ], stderr=open(log_path, "w"))

            end_time = time.time()

            if os.path.getsize(log_path) == 0:
                os.remove(log_path)
                os.remove(file_path)
                shutil.move(output_path, file_path)
                return True, end_time - start_time
            else:
                os.remove(output_path)
                continue

        except Exception as e:
            continue

    status_dict[name] = "❌ Conversione fallita"
    log_critical_error(f"Errore nella conversione/verifica: {os.path.basename(file_path)}")
    raise Exception("Errore nella conversione dopo vari tentativi")

def process_series_worker(task, status_dict):
    name = task["series"]["name"]
    try:
        episode_path, download_time = download_episode(task, status_dict)
        result, conversion_time = convert_and_verify(episode_path, status_dict, name)
        status_dict[name] = "✅ Fatto"
        return {
            "name": name, "episode": episode_path, "download_time": download_time,
            "conversion_result": result, "conversion_time": conversion_time, "error": None
        }
    except Exception as e:
        status_dict[name] = "❌ Errore"
        log_critical_error(f"{name}: {str(e)}")
        return {"name": name, "episode": None, "error": str(e)}

def display_status(status_dict, tasks_names, start_time):
    print("\033c", end="")
    elapsed = time.time() - start_time
    print(f"--- Stato Attività (Tempo: {elapsed:.0f}s) ---")
    for name in tasks_names:
        try:
            status = status_dict.get(name, '...')
        except Exception:
            status = '...'
        print(f"- {name:<25} : {status}")
    print("\nAttendere...")

def main():
    # check_dependencies()
    series_list = load_series_data()
    start_time = time.time()

    with mp.Pool(mp.cpu_count()) as pool:
        planned_tasks = pool.map(plan_series_task, series_list)

    to_process = [t for t in planned_tasks if t["action"] == "process"]
    to_skip = [t for t in planned_tasks if t["action"] == "skip"]

    print("\n--- Piano di Esecuzione ---")
    if to_process:
        for t in to_process:
            print(f"📥 {t['series']['name']} - Ep. {t['final_ep_number']}")
    else:
        print("✅ Nessun nuovo episodio da scaricare.")

    if to_skip:
        print("\n🚫 Skip:")
        for t in to_skip:
            print(f"  - {t['series']['name']}: {t['reason']}")

    if not to_process:
        return

    with mp.Manager() as manager:
        status_dict = manager.dict()
        names = [t['series']['name'] for t in to_process]

        for name in names:
            status_dict[name] = "In coda..."

        with mp.Pool(mp.cpu_count()) as pool:
            asyncs = [pool.apply_async(process_series_worker, args=(task, status_dict)) for task in to_process]

            done = 0
            while done < len(to_process):
                display_status(status_dict, names, start_time)
                done = sum(1 for r in asyncs if r.ready())
                time.sleep(1)
            results = [r.get() for r in asyncs]

    display_status(status_dict, names, start_time)
    end_time = time.time()

    print("\n--- Resoconto Finale ---")
    for r in results:
        if r["error"]:
            print(f"❌ {r['name']:<20} | Errore: {r['error']}")
        else:
            print(f"✅ {os.path.basename(r['episode']):<40} | DL: {r['download_time']:.2f}s | Conv: {r['conversion_time']:.2f}s")

    print(f"\nTempo totale: {end_time - start_time:.2f} secondi")

if __name__ == '__main__':
    main()
