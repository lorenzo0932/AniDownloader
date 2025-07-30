import json
from pathlib import Path
from PyQt6.QtWidgets import QMessageBox
from config.defaults import DEFAULT_APP_CONFIG_PATH, DEFAULT_SERIES_JSON_PATH, DEFAULT_OUTPUT_DIR, DEFAULT_LOG_FILE

class AppConfigManager:
    def __init__(self, config_path: Path = DEFAULT_APP_CONFIG_PATH):
        self._config_path = config_path
        self._config = self._load_config()

    def _load_config(self) -> dict:
        """Carica la configurazione dell'applicazione dal file o crea un default."""
        self._config_path.parent.mkdir(parents=True, exist_ok=True) # Assicura che la directory esista

        default_config = {
            "json_file_path": str(DEFAULT_SERIES_JSON_PATH),
            "output_dir": str(DEFAULT_OUTPUT_DIR),
            "log_file_path": str(DEFAULT_LOG_FILE),
            "is_json_path_customized": False
        }

        if self._config_path.exists():
            try:
                with open(self._config_path, 'r', encoding='utf-8') as f:
                    config = json.load(f)
                # Unisci la configurazione caricata con i default per garantire tutte le chiavi
                for key, value in default_config.items():
                    if key not in config:
                        config[key] = value
                return config
            except json.JSONDecodeError:
                QMessageBox.warning(None, "File di Configurazione Corrotto",
                                    f"Il file di configurazione '{self._config_path}' è corrotto o vuoto. Verrà ricreato con le impostazioni di default.")
                self._save_config(default_config)
                return default_config
        else:
            self._save_config(default_config)
            return default_config

    def _save_config(self, config_data: dict):
        """Salva la configurazione dell'applicazione nel file."""
        self._config_path.parent.mkdir(parents=True, exist_ok=True)
        with open(self._config_path, 'w', encoding='utf-8') as f:
            json.dump(config_data, f, indent=4, ensure_ascii=False)

    def get(self, key: str, default=None):
        """Restituisce un valore dalla configurazione."""
        return self._config.get(key, default)

    def set(self, key: str, value):
        """Imposta un valore nella configurazione e lo salva."""
        self._config[key] = value
        self._save_config(self._config)

    def get_all(self) -> dict:
        """Restituisce l'intera configurazione."""
        return self._config.copy()
