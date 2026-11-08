# Modelli dati

## Series (include/core/Series.hpp)

Struttura dati principale che rappresenta una serie anime.

```cpp
struct AlternateSource {
    std::string service;         // "animeW_scraper" | "animeU_scraper"
    std::string seriesPageUrl;   // URL sul servizio alternativo
};

struct Series {
    std::string name;                        // Nome visualizzato
    std::string service;                     // Scraper primario
    std::string path;                        // Cartella destinazione (può avere ~)
    bool continueSeries = true;              // Continua da stagione precedente
    bool isHighPriority = false;             // Priorità scheduling
    int passedEpisodes = 0;                  // Episodi già visti
    std::string seriesPageUrl;               // URL pagina episodi
    std::string episodeListSelector;         // (riservato)
    std::string downloadLinkSelector;        // (riservato)
    std::string lastDownloadedAt;            // Timestamp ISO8601
    int lastDownloadedEpisode = 0;           // Ultimo ep scaricato
    std::vector<AlternateSource> alternateSources;  // Fonti alternative
};
```

### Serializzazione JSON

```cpp
// from_json / to_json — conversioni automatiche con nlohmann/json
void from_json(const nlohmann::json& j, Series& s);
void to_json(nlohmann::json& j, const Series& s);
```

Il JSON è l'**export** del DB (fallback di rollback), non la fonte: le
letture runtime passano da `SeriesRepository` → SQLite.

## Persistenza SQLite (ADR-004)

`series_data.db` accanto al JSON. Schema versionato in `migrations`.

### `series`

Rispecchia 1:1 i campi di `Series`; chiave primaria `name`. Le fonti
alternative restano un sotto-documento JSON (`alternate_sources`).

| Colonna | Tipo | Note |
|---|---|---|
| `name` | TEXT PK | Nome serie |
| `service` | TEXT | Scraper primario |
| `path` | TEXT | Cartella destinazione |
| `continue_series` | INTEGER | 0/1 |
| `is_high_priority` | INTEGER | 0/1 |
| `passed_episodes` | INTEGER | Episodi già visti |
| `series_page_url` | TEXT | URL pagina serie |
| `episode_list_selector` | TEXT | Riservato |
| `download_link_selector` | TEXT | Riservato |
| `last_downloaded_at` | TEXT | ISO8601 |
| `last_downloaded_episode` | INTEGER | Ultimo episodio |
| `alternate_sources` | TEXT | JSON array |

### `tasks` (coda persistente)

| Colonna | Tipo | Note |
|---|---|---|
| `id` | INTEGER PK | Auto-increment |
| `kind` | TEXT | Oggi `check`; download/convert/notify previsti |
| `serie` | TEXT | Nome serie (vuoto per check globale) |
| `episodio` | INTEGER | 0 se non applicabile |
| `payload` | TEXT | JSON libero per il worker |
| `stato` | TEXT | `attesa`, `attivo`, `fatto`, `fallito` (CHECK) |
| `tentativi` | INTEGER | Contatore retry |
| `prossima_esecuzione` | INTEGER | Epoch unix; 0 = subito |
| `priorita` | INTEGER | Ordinamento claim |
| `creato_il` | TEXT | Timestamp |

Indice `idx_tasks_claim` su `(stato, prossima_esecuzione, priorita DESC, id)`.

### `kv` e `migrations`

`kv` conserva stato interno (es. ultimo giro scheduler); `migrations`
registra le versioni di schema applicate.

## Dati derivati

### DownloadTask (include/scrapers/BaseScraper.hpp)

Risultato del planning di uno scraper per un singolo episodio.

```cpp
struct DownloadTask {
    bool shouldProcess = false;   // true = c'è lavoro
    std::string videoUrl;         // URL diretto del video
    int episodeNumber = -1;       // Numero episodio
    std::string fileName;         // Nome file generato
    std::string errorMessage;     // Eventuale errore
};
```

### ProcessResult (include/core/MediaProcessor.hpp)

Risultato dell'elaborazione (download + conversione).

```cpp
struct ProcessResult {
    bool success = false;
    int episodeNumber = -1;
    double downloadTime = 0.0;      // secondi
    double conversionTime = 0.0;    // secondi
    std::string errorMessage;
};
```

### TaskReport (include/core/ExecutionEngine.hpp)

Report finale inviato al chiamante (main o WebServer).

```cpp
struct TaskReport {
    std::string name;
    bool success;
    int episodeNumber = -1;
    double dlTime;        // secondi
    double convTime;      // secondi
    std::string error;
};
```

### EpisodeCandidate (include/scrapers/AnimeWScraper.hpp)

Candidato episodio durante lo scraping AnimeWorld.

```cpp
struct EpisodeCandidate {
    int episodeNumber;
    std::string episodeUrl;
};
```

### EpisodeInfo (include/scrapers/ScraperUtils.hpp)

Info su episodio esistente su disco.

```cpp
struct EpisodeInfo {
    int number = 0;
    std::string path;
};
```

### DirEntry (include/core/FileUtils.hpp)

Entry directory per browser filesystem e utility.

```cpp
struct DirEntry {
    std::string name;
    std::string path;
    int64_t mtime = 0;
};
```

### UpdateInfo (include/core/UpdateChecker.hpp)

Info su aggiornamento disponibile.

```cpp
struct UpdateInfo {
    bool updateAvailable = false;
    std::string latestVersion;
    std::string downloadUrl;
};
```

### ExecutionStrategy (include/config/AppConfigManager.hpp)

Parametri di esecuzione calcolati dinamicamente.

```cpp
struct ExecutionStrategy {
    int maxConcurrentTasks;   // Task paralleli
    int chunksPerTask;        // Chunk FFmpeg (0 = auto)
    int threadsPerFFmpeg;     // Thread per FFmpeg
    bool convertToH265;       // Conversione abilitata
    bool isBurstMode;         // Modalità burst
};
```

### EmbeddedFile (include/web/embedded_web.hpp — autogenerato)

File del frontend embedded nel binario.

```cpp
struct EmbeddedFile {
    const unsigned char* data;
    size_t size;
    std::string_view mime_type;
};
```

La lookup table `getEmbeddedFiles()` restituisce una mappa
`{path → EmbeddedFile}` per servire i file statici via HTTP.
