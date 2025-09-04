import os
import time
import json
import re
import multiprocessing as mp
import shutil
import sys
from pathlib import Path

sys.path.append(str(Path(__file__).parent))

from anidownloader_config.defaults import (
    DEFAULT_SERIES_JSON_PATH, 
    DEFAULT_OUTPUT_DIR, 
    DEFAULT_LOG_FILE
)
from anidownloader_config.app_config_manager import AppConfigManager
from anidownloader_core.planning_service import plan_single_series
from anidownloader_core.media_processor import process_series_task

JSON_FILE_PATH = DEFAULT_SERIES_JSON_PATH
OUTPUT_DIR = DEFAULT_OUTPUT_DIR 
LOG_FILE = DEFAULT_LOG_FILE

def check_dependencies():
    missing = [dep for dep in ["aria2c", "ffmpeg"] if not shutil.which(dep)]
    if missing:
        print(f"ERRORE: Dipendenze di sistema mancanti nel PATH: {', '.join(missing)}.")
        sys.exit(1)
    print("✅ Dipendenze di sistema trovate.")

def load_series_data():
    try:
        JSON_FILE_PATH.parent.mkdir(parents=True, exist_ok=True)
        with open(JSON_FILE_PATH, 'r', encoding='utf-8') as f:
            return json.load(f)
    except FileNotFoundError:
        print(f"INFO: File di configurazione '{JSON_FILE_PATH}' non trovato. Eseguire la GUI almeno una volta per crearlo.")
        return []
    except Exception as e:
        print(f"ERRORE: Impossibile caricare '{JSON_FILE_PATH}': {e}")
        sys.exit(1)

def display_status(status_dict, tasks_names, start_time):
    print("\033c", end="")
    elapsed = time.time() - start_time
    print(f"--- Stato Attività (Tempo: {elapsed:.0f}s) ---")
    for name in tasks_names:
        print(f"- {name:<35} : {status_dict.get(name, '...')}")
    print("\nLavori in corso...")

class CLIStatusUpdater:
    def __init__(self, status_dict):
        self._status_dict = status_dict
    def update_progress(self, series_name: str, message: str):
        self._status_dict[series_name] = message
    def report_error(self, series_name: str, error_message: str):
        self._status_dict[series_name] = f"❌ Errore: {error_message}"

def main():
    check_dependencies()
    
    # --- MODIFICA CHIAVE: Uso corretto di AppConfigManager ---
    print("Caricamento configurazione applicazione...")
    try:
        # 1. Crea un'istanza. Il costruttore carica automaticamente il file.
        config_manager = AppConfigManager()
        # 2. Usa il metodo 'get' per leggere l'impostazione.
        convert_to_h265 = config_manager.get('convert_to_h265', False)
        print(f"ℹ️ Conversione H.265: {'Abilitata' if convert_to_h265 else 'Disabilitata'}")
    except Exception as e:
        # Aggiungiamo un traceback per un debug più facile in caso di errori imprevisti
        import traceback
        print(f"ATTENZIONE: Impossibile caricare config app. Conversione disabilitata.")
        print(traceback.format_exc())
        convert_to_h265 = False
    # --- FINE MODIFICA ---

    series_list = load_series_data()
    if not series_list:
        print("Nessuna serie configurata. Uscita."); return

    start_time = time.time()

    print("Pianificazione attività in corso...")
    with mp.Pool(mp.cpu_count()) as pool:
        planned_tasks = pool.map(plan_single_series, series_list)

    to_process = [t for t in planned_tasks if t["action"] == "process"]
    to_skip = [t for t in planned_tasks if t["action"] == "skip"]

    print("\n--- Piano di Esecuzione ---")
    if to_process:
        for t in to_process: print(f"📥 {t['series']['name']} - {t['reason']}")
    else:
        print("✅ Nessun nuovo episodio da scaricare.")

    if to_skip:
        print("\n🚫 Serie saltate:")
        for t in to_skip: print(f"  - {t['series']['name']}: {t['reason']}")

    if not to_process:
        return

    for task in to_process:
        series = task["series"]
        download_url = task["download_url"]
        final_ep_number = task["final_ep_number"]
        url_filename = download_url.split("/")[-1].split("?")[0]
        filename_root = series.get("filename_root")
        if not filename_root:
            match = re.match(r'(.*?)_Ep_', url_filename, re.IGNORECASE)
            filename_root = match.group(1) if match else os.path.splitext(url_filename)[0]
        suffix_match = re.search(r'(_Ep_.*)', url_filename, re.IGNORECASE)
        if suffix_match:
            suffix = suffix_match.group(1)
            correct_suffix = re.sub(r'(\d+)', f'{final_ep_number:02d}', suffix, 1)
            task["final_filename"] = filename_root + correct_suffix
        else:
            task["final_filename"] = f"{filename_root}_Ep_{final_ep_number:02d}.mp4"
    
    with mp.Manager() as manager:
        status_dict = manager.dict({t['series']['name']: "In coda..." for t in to_process})
        names = [t['series']['name'] for t in to_process]
        
        cli_status_updater = CLIStatusUpdater(status_dict)
        stop_event = manager.Event()
        
        results = []
        with mp.Pool(mp.cpu_count()) as pool:
            pool_args = [(task, OUTPUT_DIR, LOG_FILE, cli_status_updater, stop_event, convert_to_h265) for task in to_process]
            async_results = pool.starmap_async(process_series_task, pool_args)

            try:
                while not async_results.ready():
                    display_status(status_dict, names, start_time)
                    time.sleep(1)
                
                results = async_results.get()
                
                for r in results:
                    if r and r.get("name"):
                        if r.get("error"):
                            status_dict[r['name']] = "❌ Errore"
                        else:
                            status_dict[r['name']] = "✅ Fatto"
            except KeyboardInterrupt:
                print("\nInterruzione richiesta dall'utente... Chiusura dei processi.")
                stop_event.set()
                pool.terminate()
                pool.join()
                print("Processi terminati.")
                sys.exit(1)

        display_status(status_dict, names, start_time)
        end_time = time.time()

        print("\n\n--- Resoconto Finale ---")
        for r in results:
            if r:
                if r["error"]:
                    print(f"❌ {r['name']:<30} | Errore: {r['error']}")
                else:
                    print(f"✅ {Path(r['episode']).name:<50} | DL: {r['download_time']:.2f}s | Conv: {r['conversion_time']:.2f}s")

        print(f"\nTempo totale: {end_time - start_time:.2f} secondi")

if __name__ == '__main__':
    main()