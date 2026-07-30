# Configurazione

## File di configurazione

| File | Path | Descrizione |
|------|------|-------------|
| `config.json` | `~/.config/AniDownloader/config.json` | Configurazione globale |
| `series_data.json` | `~/.config/AniDownloader/series_data.json` | Dati delle serie (NON committare) |

I path seguono lo standard XDG su Linux (`~/.config/`) e APPDATA su Windows
(`%APPDATA%/AniDownloader/`).

---

## config.json

Auto-generato al primo avvio. Supporta i seguenti campi:

| Campo | Tipo | Default | Descrizione |
|-------|------|---------|-------------|
| `max_network_retries` | int | `3` | Tentativi massimi per HTTP/aria2c |
| `retry_delay_ms` | int | `2000` | Delay iniziale tra retry (raddoppia ogni tentativo) |
| `convert_to_h265` | bool | `true` | Conversione HEVC post-download |
| `num_chunks` | int | `0` | Chunk FFmpeg (`0` = auto, `1` = no chunk, `>1` = chunk parallelo) |
| `auto_cleanup_on_close` | bool | `true` | Pulizia file parziali alla chiusura |
| `chromedriver_path` | string | `chromedriver` | Path o nome del binary ChromeDriver |

---

## series_data.json

Array di oggetti serie:

```json
[
    {
        "name": "Nome Serie",
        "path": "~/Video/Anime/Nome Serie",
        "series_page_url": "https://animeworld.so/...",
        "service": "animeW_scraper",
        "continue": false,
        "is_high_priority": false,
        "passed_episodes": 0,
        "last_downloaded_at": "2026-07-28T15:30:00Z",
        "last_downloaded_episode": 15,
        "alternate_sources": [
            {
                "service": "animeU_scraper",
                "series_page_url": "https://animeunity.to/..."
            }
        ]
    }
]
```

### Campi

| Campo | Tipo | Obbligatorio | Descrizione |
|-------|------|-------------|-------------|
| `name` | string | ✅ | Nome visualizzato |
| `path` | string | ✅ | Cartella di destinazione (supporta `~`) |
| `series_page_url` | string | ✅ | URL della pagina serie sul sito streaming |
| `service` | string | ✅ | `animeW_scraper` o `animeU_scraper` |
| `continue` | bool | ❌ | `true` se la serie continua una stagione precedente |
| `is_high_priority` | bool | ❌ | `true` per priorità alta nello scheduling |
| `passed_episodes` | int | ❌ | Episodi già visti (se `continue: true`) |
| `last_downloaded_at` | string | ❌ | Timestamp ISO8601 ultimo download |
| `last_downloaded_episode` | int | ❌ | Ultimo episodio scaricato |
| `alternate_sources` | array | ❌ | Fonti alternative per fallback |

### alternate_sources[]

| Campo | Tipo | Obbligatorio | Descrizione |
|-------|------|-------------|-------------|
| `service` | string | ✅ | `animeW_scraper` o `animeU_scraper` |
| `series_page_url` | string | ✅ | URL della pagina sulla fonte alternativa |

---

## ExecutionStrategy

Generata dinamicamente da `AppConfigManager::getExecutionStrategy()`
in base al numero di task in coda e alla CPU rilevata.

```cpp
struct ExecutionStrategy {
    int maxConcurrentTasks;   // task simultanei
    int chunksPerTask;        // chunk FFmpeg (0/1 = no chunk)
    int threadsPerFFmpeg;     // thread per processo FFmpeg
    bool convertToH265;       // conversione abilitata
    bool isBurstMode;         // burst = priorità max, silent = nice 19
};
```

### Logica burst vs silent

| Modalità | `maxConcurrentTasks` | `threadsPerFFmpeg` | nice |
|----------|---------------------|--------------------|------|
| Burst | cores × 2 | cores / 2 | normale |
| Silent | cores / 2 | cores / 4 | `nice -n 19` |

### Logica chunking

`numChunks = 0` (auto):
- Se task ≤ maxConcurrentTasks: `chunksPerTask = max(1, cores / 2)`
- Se task > maxConcurrentTasks: `chunksPerTask = 0` (direct encoding)

Il chunking parallelo migliora l'encoding per singoli file grandi,
ma consuma più memoria e thread.
