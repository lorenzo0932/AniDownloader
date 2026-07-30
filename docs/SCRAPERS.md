# Sistema Scraper

Namespace `Core::` — classi `BaseScraper`, `AnimeWScraper`, `AnimeUScraper`, `ScraperUtils`.

## Architettura

```
BaseScraper (classe astratta)
  ├── AnimeWScraper  → animeworld.so
  └── AnimeUScraper  → animeunity.to
```

Ogni scraper concreto implementa:

```cpp
std::vector<DownloadTask> planSeriesTask(const Series& series,
                                         std::atomic<bool>& stopSignal,
                                         ScraperProgressCb progressCb = nullptr);
```

### DownloadTask

```cpp
struct DownloadTask {
    bool shouldProcess = false;   // true = c'è roba da scaricare
    std::string videoUrl;         // URL diretto del video
    int episodeNumber = -1;
    std::string fileName;         // nome file generato
    std::string errorMessage;     // se errore
};
```

## Planning flusso

```
PlanningService::planSingleSeries()
  │
  ├── getScraperInstance(serviceName)
  │   └── return new AnimeWScraper / AnimeUScraper
  │
  └── scraper->planSeriesTask(series)
        │
        ├── ScraperUtils::computeNextNeeded()
        │   ├── episodesMap = scanEpisodesMap(path)
        │   ├── se lastDownloadedEpisode > 0:
        │   │   search backward → primo valido + 1
        │   ├── se lastDownloadedEpisode ≤ 0:
        │   │   fallback a getHighestEpisodeFile() + 1
        │   └── se nessun valido → ep 1
        │
        ├── ChromeDriver → naviga pagina episodi
        │   ├── trova lista episodi
        │   ├── estrae URL video diretto
        │   └── genera nome file
        │
        └── restituisce DownloadTask[..]
```

## ChromeDriver

Entrambi gli scraper usano ChromeDriver per il rendering JavaScript
(necessario per i player embedded dei siti).

- **Porta**: 9515 (default ChromeDriver)
- **Avvio**: automatico all'inizio di `ExecutionEngine::run()`
  - Linux: `pgrep chromedriver || chromedriver --port=9515 &`
  - Windows: `tasklist | find chromedriver || start chromedriver --port=9515`
- **Concorrenza**: limitata da `ScraperSemaphoreGuard` (semforo globale RAII)

### ScraperSemaphoreGuard

```cpp
class ScraperSemaphoreGuard {
    // Limita a MAX_CONCURRENT sessioni ChromeDriver simultanee
    // RAII: costruttore acquire, distruttore release
};
```

## ScraperUtils

`include/scrapers/ScraperUtils.hpp` — `src/scrapers/ScraperUtils.cpp`

Utilità condivise tra scraper:

### Filesystem

| Funzione | Descrizione |
|----------|-------------|
| `expandTilde(path)` | `~/...` → `/home/user/...` |
| `getHighestEpisodeFile(path)` | Trova l'episodio con numero più alto |
| `getNextEpisodeNum(path)` | Prossimo numero episodio da scaricare |
| `scanEpisodesMap(path)` | Mappa `{epNumber → fullPath}` |
| `validEpisodePath(map, ep)` | Verifica se esiste valido |
| `computeNextNeeded(path, lastEp, map)` | Calcolo prossimo episodio |

### HTTP

| Funzione | Descrizione |
|----------|-------------|
| `httpGetWithRetry(url, headers, retry, delay)` | GET HTTP con retry |

### Shell

| Funzione | Descrizione |
|----------|-------------|
| `Q(path)` | Quoting per shell (diverso per POSIX/Windows) |
| `popenCompat()` | popen cross-platform |
| `DEVNULL()` | `/dev/null` o `NUL` |
| `platformUserAgent()` | User-Agent dinamico per piattaforma |

## ComputeNextNeeded — logica

```
computeNextNeeded(path, lastDownloadedEpisode, episodesMap):

1. Se lastDownloadedEpisode > 0:
   - Cerca da N in giù finché trova un file esistente e valido
   - Restituisce quello + 1

2. Se lastDownloadedEpisode <= 0:
   - getHighestEpisodeFile() + 1

3. Se nessun episodio valido trovato:
   - Restituisce 1
```

## Aggiungere uno scraper

1. Crea `AnimeXScraper.hpp` in `include/scrapers/`
2. Estendi `BaseScraper`
3. Implementa `planSeriesTask()`
4. Aggiungi in `PlanningService::getScraperInstance()`
5. Aggiungi file a `CMakeLists.txt`

```cpp
class AnimeXScraper : public BaseScraper {
    std::vector<DownloadTask> planSeriesTask(const Series& series,
        std::atomic<bool>& stopSignal,
        ScraperProgressCb progressCb = nullptr) override;
};
```
