import importlib

# Mappa che associa il valore del campo "service" nel JSON al nome della Classe Scraper
SCRAPER_CLASS_MAP = {
    'AnimeW_scraper': 'AnimeWScraper'
    # Esempio per il futuro:
    # 'AltroSito_scraper': 'AltroSitoScraper' 
}

def get_scraper_instance(service_name: str):
    """
    Carica dinamicamente il modulo dello scraper e restituisce un'istanza della classe.
    """
    if service_name not in SCRAPER_CLASS_MAP:
        raise ValueError(f"Servizio '{service_name}' non riconosciuto. Assicurati che sia definito in SCRAPER_CLASS_MAP.")
    
    class_name = SCRAPER_CLASS_MAP[service_name]
    
    try:
        # Importa il modulo (es. scrapers.AnimeW_scraper)
        module = importlib.import_module(f"scrapers.{service_name}")
        # Ottiene la classe dal modulo (es. AnimeWScraper)
        ScraperClass = getattr(module, class_name)
        return ScraperClass()
    except (ImportError, AttributeError) as e:
        raise ImportError(f"Impossibile caricare lo scraper '{class_name}' dal modulo '{service_name}': {e}")


def plan_single_series(series: dict):
    """
    Funzione wrapper che sceglie lo scraper giusto e pianifica una singola serie.
    Questa è la funzione che verrà chiamata dal Pool di multiprocessing.
    """
    service = series.get("service")
    if not service:
        return { "series": series, "action": "skip", "reason": "Campo 'service' non specificato nel JSON." }
    
    try:
        scraper = get_scraper_instance(service)
        return scraper.plan_series_task(series)
    except Exception as e:
        return { "series": series, "action": "skip", "reason": f"Errore durante la pianificazione: {e}" }