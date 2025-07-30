import json
from pathlib import Path

class SeriesRepository:
    def __init__(self, json_file_path: Path):
        self._json_file_path = json_file_path

    def load_series_data(self) -> list:
        """Carica i dati delle serie dal file JSON."""
        if not self._json_file_path.exists():
            # Se il file non esiste, crea un file JSON vuoto
            self._json_file_path.parent.mkdir(parents=True, exist_ok=True)
            with open(self._json_file_path, 'w', encoding='utf-8') as f:
                json.dump([], f, indent=4)
            return []
        
        try:
            with open(self._json_file_path, 'r', encoding='utf-8') as f:
                return json.load(f)
        except json.JSONDecodeError as e:
            raise Exception(f"Errore di decodifica JSON nel file '{self._json_file_path}': {e}")
        except Exception as e:
            raise Exception(f"Errore durante il caricamento del file '{self._json_file_path}': {e}")

    def save_series_data(self, series_data: list):
        """Salva i dati delle serie nel file JSON."""
        self._json_file_path.parent.mkdir(parents=True, exist_ok=True)
        try:
            with open(self._json_file_path, 'w', encoding='utf-8') as f:
                json.dump(series_data, f, indent=4, ensure_ascii=False)
        except Exception as e:
            raise Exception(f"Errore durante il salvataggio del file '{self._json_file_path}': {e}")
