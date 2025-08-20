from abc import ABC, abstractmethod

class BaseScraper(ABC):
    """
    Classe base astratta per tutti gli scraper.
    Definisce l'interfaccia che ogni scraper di sito deve implementare.
    """
    
    @abstractmethod
    def plan_series_task(self, series: dict) -> dict:
        """
        Analizza una singola serie per determinare se c'è un nuovo episodio da scaricare.

        Args:
            series (dict): Il dizionario di configurazione per una singola serie.

        Returns:
            dict: Un dizionario "task" che contiene l'azione da intraprendere 
                  ('process' o 'skip') e tutte le informazioni necessarie per il download.
        """
        pass