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

# --- CONFIGURAZIONE ---
JSON_FILE_PATH = 'series_data.json'
LOG_FILE = 'serie_critical_errors.log'

def check_dependencies():
    missing_dependencies = []
    if not shutil.which("aria2c"):
        missing_dependencies.append("aria2c")
    if not shutil.which("ffmpeg"):
        missing_dependencies.append("ffmpeg")

    if missing_dependencies:
        print(f"ERRORE: Le seguenti dipendenze non sono state trovate nel PATH: {', '.join(missing_dependencies)}.")
        print("Questi sono strumenti a riga di comando e non possono essere installati tramite pip.")
        print("Si prega di installarli manualmente utilizzando il gestore di pacchetti del sistema (es. apt, yum, brew, winget) o scaricando i binari.")
        print("\nEsempi di installazione:")
        print("  - Debian/Ubuntu: sudo apt install aria2 ffmpeg")
        print("  - Fedora: sudo dnf install aria2 ffmpeg")
        print("  - macOS (Homebrew): brew install aria2 ffmpeg")
        print("  - Windows (Winget): winget install aria2; winget install ffmpeg")
        sys.exit(1)
    print("✅ Dipendenze trovate.")

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

def format_episode_number(ep_num):
    return f"{ep_num:02d}"

def plan_series_task(series):
    name = series["name"]
    path = series["path"]
    link_pattern = series["link_pattern"]
    is_continuation = series.get("continue", False)
    passed_episodes = series.get("passed_episodes", 0)

    final_ep_number = get_next_episode_num(path)
    if is_continuation and final_ep_number <= passed_episodes:
        final_ep_number = passed_episodes + 1
    next_ep_download = final_ep_number if not is_continuation else final_ep_number - passed_episodes

    ep_str_download = format_episode_number(next_ep_download)
    download_url = link_pattern.format(ep=ep_str_download)

    task = {
        "series": series,
        "action": "skip",
        "reason": "URL non raggiungibile.",
        "download_url": download_url,
        "next_ep_download": next_ep_download,
        "final_ep_number": final_ep_number
    }

    try:
        response = requests.head(download_url, timeout=10, allow_redirects=True)
        if response.status_code == 200:
            task["action"] = "process"
            task["reason"] = f"Pronto per scaricare Ep. {final_ep_number}"
        else:
            task["reason"] = f"HTTP {response.status_code}"
    except requests.RequestException:
        pass

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
    
    downloaded_file_name = download_url.split("/")[-1].split("?")[0]
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
            if not line:
                break
            #print(f"aria2c: {line.strip()}")
            #match = re.search(r'\s(\d+)%', line)
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

    final_file_path = output_file_path
    if series.get("continue", False):
        ep_str_download = format_episode_number(task["next_ep_download"])
        ep_str_final = format_episode_number(task["final_ep_number"])
        
        new_filename = re.sub(f'Ep[._-]?{ep_str_download}', f'Ep_{ep_str_final}', output_file_path.name, flags=re.IGNORECASE)
        final_file_path = output_file_path.with_name(new_filename)
        os.rename(output_file_path, final_file_path)

    return str(final_file_path), end_time - start_time

def convert_and_verify(file_path, status_dict, name, max_retries=3):
    output_dir = "/home/lorenzo/Video/Convertiti"
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

            # Verifica
            verify = subprocess.run([
                "nice", "-n", "5", "ffmpeg", "-y", "-v", "error", "-i",
                output_path, "-f", "null", "-"
            ], stderr=open(log_path, "w"))

            end_time = time.time()

            if os.path.getsize(log_path) == 0:
                # Conversione ok
                os.remove(log_path)
                os.remove(file_path)
                shutil.move(output_path, file_path)
                return True, end_time - start_time
            else:
                # Errore, cancella output e riprova
                os.remove(output_path)
                # Non stampiamo nulla, solo aggiorniamo status_dict con tentativo
                continue

        except Exception as e:
            # Qui puoi anche decidere se loggare o ignorare
            continue

    # Se siamo qui, dopo tutti i tentativi
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
    # Cancella schermo con ANSI escape
    print("\033c", end="")  # Questo resetta lo schermo

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
    check_dependencies()
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
