#! /usr/bin/python3

import os
import re
import requests
import subprocess
import sys
from pathlib import Path
import shutil
import time
import multiprocessing as mp
import json


# --- Caricamento dati delle serie dal file JSON ---
JSON_FILE_PATH = 'series_data.json' # Definisci il percorso del file JSON

try:
    with open(JSON_FILE_PATH, 'r', encoding='utf-8') as f: # Apri il file in lettura ('r') con encoding UTF-8
        series_list = json.load(f) # Carica i dati JSON in una variabile Python
except FileNotFoundError:
    print(f"Errore: File JSON '{JSON_FILE_PATH}' non trovato.")
    sys.exit(1) # Esce dallo script se il file non viene trovato
except json.JSONDecodeError as e:
    print(f"Errore: Il file JSON '{JSON_FILE_PATH}' non è formattato correttamente.")
    print(f"Dettagli errore: {e}")
    sys.exit(1) # Esce dallo script se il JSON è invalido
except Exception as e:
    print(f"Errore inaspettato durante la lettura del file JSON: {e}")
    sys.exit(1)
# ----------------------------------------------------


def get_next_episode(series_path):
    """
        Cerca nella cartella indicata (non ricorsivamente) tutti i file .mp4 e restituisce il numero
        dell'episodio successivo da scaricare (max trovato + 1). Se non vengono trovati file, restituisce 1.
        """
    files = os.listdir(series_path)
    max_ep = 0
    for file in files:
        if file.endswith('.mp4'):
            # Estrae il numero dell'episodio cercando la stringa "Ep_" seguita da uno o più numeri
            match = re.search(r'Ep_(\d+)', file)
            if match:
                ep_num = int(match.group(1))
                if ep_num > max_ep:
                    max_ep = ep_num
    return max_ep + 1 if max_ep > 0 else 1


def format_episode_number(ep):
    """
        Formattta il numero dell'episodio:
        - Se è inferiore a 10, restituisce una stringa con padding (es. "01")
        - Altrimenti restituisce la stringa senza padding (es. "10")
        """
    return f"{ep:02d}" if ep < 10 else str(ep)


def download_episode_with_aria2(series):
    """
        Costruisce l'URL di download sostituendo il placeholder {ep} nel pattern, quindi
        utilizza aria2c per scaricare il file in parallelo.
        Gestisce anche il rinomino dei file in caso di 'continue' attivo.
        """
    name = series["name"]
    path = series["path"]
    link_pattern = series["link_pattern"]
    is_continuation = series.get("continue", False)  # Default to False if not present
    passed_episodes = series.get("passed_episodes", 0)  # Default to 0 if not present

    # Calcola il prossimo episodio da scaricare (numerazione interna alla stagione) e se è una continuazione il numero finale è calcolato in base al numero di episodi passati
    if is_continuation:
        final_ep_number = get_next_episode(path)
        if final_ep_number <= passed_episodes:
            final_ep_number = passed_episodes + 1
        print(f"{final_ep_number} {passed_episodes}")
        next_ep_download = final_ep_number - passed_episodes
    else :
        next_ep_download = get_next_episode(path)
        ep_str_download = format_episode_number(next_ep_download)

    # Costruisce l'URL sostituendo il placeholder con la numerazione per il download
    ep_str_download = format_episode_number(next_ep_download)
    download_url = link_pattern.format(ep=ep_str_download)
    print(f"[{name} Tentativo di download episodio {next_ep_download} (download num) -> URL: {download_url}")

    # Estrae il nome del file dalla URL
    downloaded_file_name = download_url.split("/")[-1]
    output_file_path_download = os.path.join(path, downloaded_file_name)

    # Costruisce il comando aria2c; -x e -s definiscono il numero di connessioni parallelle
    cmd = ["aria2c", "-x", "16", "-s", "16", "-o", downloaded_file_name, download_url]
    print(f"[{name}] Avvio aria2c per il download: {' '.join(cmd)}")

    # Imposta la directory corrente per il comando in modo che il file venga salvato in 'path'
    start_time = time.time()
    result = subprocess.run(cmd, cwd=path)
    end_time = time.time()
    download_time = end_time - start_time

    if result.returncode == 0:
        print(f"[{name}] Episodio {next_ep_download} (download num) scaricato con successo!")

        final_file_path = output_file_path_download  # Initialize with download path

        if is_continuation:
            # Calcola il numero di episodio finale (considerando gli episodi precedenti)
            ep_str_final = format_episode_number(final_ep_number)

            # Costruisci il nome file finale basato sul pattern e il numero episodio finale
            base_filename = downloaded_file_name.replace(f"_Ep_{ep_str_download}",
                                                           f"_Ep_{ep_str_final}")  # Simple replace, might need more robust logic if filenames are very different

            final_file_path = os.path.join(path, base_filename)

            # Rinomina il file
            try:
                os.rename(output_file_path_download, final_file_path)
                print(f"[{name}] File rinominato in: {final_file_path}")
            except OSError as e:
                print(f"[{name}] Errore durante il rinomino del file: {e}")
                return None, None  # Return None to indicate failure

        return final_file_path, download_time
    else:
        print(f"[{name}] Download fallito con aria2c (return code: {result.returncode}).")
        return None, None


def convertToH265(episode_path):
    # Salvo il nome del file
    file_basename = os.path.basename(episode_path)

    # Imposta la variabile di ambiente che si aspetta Nautilus
    env = os.environ.copy()
    env["NAUTILUS_SCRIPT_SELECTED_FILE_PATHS"] = episode_path

    # Specifica il path dello scritp Nautilus. Lo script converte il file, verifica la conversione ed elimina l'originale.
    script_path = "./Converti e verifica.sh"

    # Verifico che lo script da eseguire esista e sia eseguibile
    if not os.path.isfile(script_path):
        print(f"Lo script {script_path} non esiste")
        return None, None

    if not os.access(script_path, os.X_OK):
        print(f"Lo script {script_path} non è eseguibile")
        return None, None

    success = 0
    count = 1
    start_time = time.time()
    while success == 0 and count <= 2:
        # Avvia lo script passando l'ambiente con la variabile di ambiente impostata in precedenza
        result = subprocess.run([script_path], env=env)
        # Se il file non è stato eliminato secondo le specifiche di Converti e Verifica.sh allora la conversione è fallita
        if os.path.exists(episode_path):
            print("Conversione fallita, riprova")
            count = count + 1
        else:
            success = 1
    end_time = time.time()
    conversion_time = end_time - start_time

    if success == 0:
        print("Errore: conversione non riuscita dopo 3 tentativi")
        return None, None

    # Se la conversione è avvenuta con successo, sposta il file convertito nella directory corretta
    converted_file_path = f"/run/media/lorenzo/SSD Sata/Convertiti/{file_basename}"
    shutil.move(converted_file_path, episode_path)
    return True, conversion_time


def process_series(series):
    """
    Processa una singola serie: download e conversione.
    Restituisce un dizionario con i risultati.
    """
    name = series["name"]
    print(f"Inizio processo per la serie: {name}")
    try:
        # Scarica un episodio se necessario e lo converte in h265
        Episode, download_time = download_episode_with_aria2(series)

        if Episode:
            conversion_result, conversion_time = convertToH265(Episode)
            return {
                "name": name,
                "episode": Episode,
                "download_time": download_time,
                "conversion_result": conversion_result,
                "conversion_time": conversion_time
            }
        else:
            return {
                "name": name,
                "episode": None,
                "download_time": 0,
                "conversion_result": False,
                "conversion_time": 0
            }
    except Exception as e:
        print(f"Errore durante il processamento della serie {name}: {e}")
        return {
            "name": name,
            "episode": None,
            "download_time": 0,
            "conversion_result": False,
            "conversion_time": 0,
            "error": str(e)
        }


if __name__ == '__main__':
    # Inizializzazione delle variabili per il resoconto
    downloaded_episodes = []
    conversion_successes = []
    download_times = {}
    conversion_times = {}
    total_conversion_time = 0
    total_download_time = 0
    script_start_time = time.time()

    # Utilizza un Pool di processi per eseguire il download e la conversione in parallelo
    with mp.Pool(mp.cpu_count()) as pool:  # Utilizza tutti i core disponibili
        results = pool.map(process_series, series_list)

    # Elabora i risultati
    for result in results:
        if result["episode"]:
            downloaded_episodes.append(result["episode"])
            download_times[result["episode"]] = result["download_time"]
            total_download_time += result["download_time"]

            if result["conversion_result"]:
                conversion_successes.append(result["episode"])
                conversion_times[result["episode"]] = result["conversion_time"]
                total_conversion_time += result["conversion_time"]
            else:
                conversion_successes.append(False)

    script_end_time = time.time()
    script_total_time = script_end_time - script_start_time

    # Calcola la velocità media di download (se ci sono episodi scaricati)
    if downloaded_episodes:
        # Ottieni la dimensione totale dei file scaricati
        total_size = sum([os.path.getsize(ep) for ep in downloaded_episodes])
        average_download_speed = total_size / total_download_time if total_download_time > 0 else 0
        # Converto da byte/secondo a MB/secondo
        average_download_speed_mbps = average_download_speed / (1024 * 1024)
    else:
        average_download_speed_mbps = 0

    # Stampa il resoconto
    print("\n--- Resoconto ---")
    print("Episodi scaricati:")
    for episode in downloaded_episodes:
        print(f"- {episode}")

    print("\nStato conversioni:")
    for episode in downloaded_episodes:
        if episode in conversion_successes:
            print(f"- {episode}: Successo")
        else:
            print(f"- {episode}: Fallimento")

    print(f"\nVelocità media di download: {average_download_speed_mbps:.2f} MB/s")
    print(f"Tempo totale di esecuzione dello script: {script_total_time:.2f} secondi")

    print("\nDettagli tempi:")
    print("- Tempi download per episodio:")
    for episode, time in download_times.items():
        print(f"  - {episode}: {time:.2f} secondi")

    print("- Tempi conversione per episodio:")
    for episode, time in conversion_times.items():
        print(f"  - {episode}: {time:.2f} secondi")

    print(f"- Tempo totale solo conversioni: {total_conversion_time:.2f} secondi")
    print(f"- Tempo totale solo download: {total_download_time:.2f} secondi")