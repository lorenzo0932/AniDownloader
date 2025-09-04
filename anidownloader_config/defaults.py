from pathlib import Path
from platformdirs import user_videos_dir, user_config_dir, user_log_dir

# Definisci il nome della tua applicazione una sola volta
APP_NAME = "AniDownloader"

# --- Percorsi Dinamici ---

# Directory di output per i video convertiti
# Trova la cartella "Video" predefinita dell'utente e aggiunge la sottocartella "Convertiti"
DEFAULT_OUTPUT_DIR = Path(user_videos_dir()) / "Convertiti"

# Directory di configurazione specifica per l'applicazione
# Segue le convenzioni di ogni sistema operativo
DEFAULT_CONFIG_DIR = Path(user_config_dir(appname=APP_NAME))

# File di log specifico per l'applicazione
# Utilizza la directory dei log standard per l'utente
DEFAULT_LOG_FILE = Path(user_log_dir(appname=APP_NAME)) / 'serie_critical_errors.log'

# Percorsi dei file JSON, basati sulla nuova directory di configurazione
DEFAULT_SERIES_JSON_PATH = DEFAULT_CONFIG_DIR / "series_data.json"
DEFAULT_APP_CONFIG_PATH = DEFAULT_CONFIG_DIR / "config.json"


# --- Verifica e Creazione delle Directory (Opzionale ma consigliato) ---
# È buona norma assicurarsi che le directory esistano prima di usarle.
DEFAULT_CONFIG_DIR.mkdir(parents=True, exist_ok=True)
DEFAULT_LOG_FILE.parent.mkdir(parents=True, exist_ok=True)


# Esempio di come stampare i percorsi generati
if __name__ == "__main__":
    print(f"Directory di Output: {DEFAULT_OUTPUT_DIR}")
    print(f"Directory di Configurazione: {DEFAULT_CONFIG_DIR}")
    print(f"File di Log: {DEFAULT_LOG_FILE}")
    print(f"Percorso JSON Serie: {DEFAULT_SERIES_JSON_PATH}")
    print(f"Percorso Config App: {DEFAULT_APP_CONFIG_PATH}")