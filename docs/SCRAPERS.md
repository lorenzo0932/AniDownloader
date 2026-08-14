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
        ├── AnimeW: lista episodi (regex) + endpoint statico del player
        │   ├── GET /api/episode/info?id=<episodeId>&alt=1 → {"grabber": ...}
        │   ├── grabber = URL diretto del video sul CDN (sweetpixel)
        │   └── genera nome file dal pattern del CDN
        │
        └── restituisce DownloadTask[..]
```

## Flusso statico AnimeW (niente browser)

Dalla feature 6 il flusso di AnimeW è **interamente statico** (nessuna
dipendenza da ChromeDriver/browser):

1. `getCandidates`: GET sulla pagina serie → regex `data-episode-num` + `href`
   (lista episodi, id = ultimo segmento dell'URL pagina episodio).
2. Per ogni episodio mancante: `GET https://www.animeworld.ac/api/episode/info?id=<id>&alt=1`
   con UA + Referer statici → risposta `{"grabber": "<URL video>", ...}`.
3. `grabber` → `DownloadTask` (URL predicibile del CDN, es.
   `https://srv<NN>-<name>.sweetpixel.org/DDL/ANIME/<Romaji>/<Romaji>_Ep_<NN>_SUB_ITA.mp4`).

Fallimenti rumorosi (feature 5): API fallita / grabber assente → `Logger::warn`
con status e contesto; id inesistente → `{"error":true}`.

Per i test offline l'endpoint è sovrascrivibile con la variabile d'ambiente
`ANIDOWNLOADER_API_BASE` (usata dallo smoke per la fixture locale).

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
