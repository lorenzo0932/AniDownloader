# Configurazione

## File di configurazione

| File | Path | Descrizione |
|------|------|-------------|
| `config.json` | `~/.config/AniDownloader/config.json` | Configurazione globale |
| `series_data.db` | `~/.config/AniDownloader/series_data.db` | **Fonte autorevole** delle serie (SQLite, ADR-004) |
| `series_data.json` | `~/.config/AniDownloader/series_data.json` | Export del DB, usato come fallback di rollback (NON committare) |
| `series_data.json.bak.<timestamp>` | accanto al JSON | Backup creato prima di un import JSON→DB |

I path seguono lo standard XDG su Linux (`~/.config/`) e APPDATA su Windows
(`%APPDATA%/AniDownloader/`).

---

## config.json

Auto-generato al primo avvio. Supporta i seguenti campi:

| Campo | Tipo | Default | Descrizione |
|-------|------|---------|-------------|
| `json_file_path` | string | percorso XDG | File JSON delle serie (export/fallback) |
| `output_dir` | string | cartella Video | Destinazione predefinita dei download |
| `log_file_path` | string | percorso XDG | File di log |
| `is_json_path_customized` | bool | `false` | Indica un path JSON scelto dall'utente |
| `max_network_retries` | int | `3` | Tentativi massimi per HTTP/aria2c |
| `retry_delay_ms` | int | `2000` | Delay iniziale tra retry (raddoppia ogni tentativo) |
| `convert_to_h265` | bool | `true` | Conversione HEVC post-download |
| `burst_on_manual_start` | bool | `true` | Download avviati a mano dalla UI: risorse piene (85% CPU) invece del ritmo contenuto del background. Lo scheduler non legge questa chiave |
| `num_chunks` | int | `0` | Chunk FFmpeg (`0` = auto, `1` = no chunk, `>1` = chunk parallelo) |
| `auto_cleanup_on_close` | bool | `true` | Pulizia file parziali alla chiusura (deprecata: vince `resume_interrupted_downloads`) |
| `resume_interrupted_downloads` | bool | `true` | Riprende i download interrotti tra un run e l'altro |
| `scheduling.abilitato` | bool | `true` | Controllo periodico interno al demone |
| `scheduling.intervalloMinuti` | int | `15` | Intervallo controlli (5–1440 minuti) |
| `notifiche.abilitate` | bool | `false` | Master ON/OFF notifiche (vedi sotto) |
| `notifiche.sorgente` | string | `entrambi` | `ui`, `backend` o `entrambi` |
| `pinned_paths` | array | assente (UI) | Preferiti del file picker (max 20, scritti dalla UI) |
| `show_stop_warning` | bool | `true` (UI) | Conferma prima di interrompere un download |
| `show_close_warning` | bool | `true` (UI) | Conferma alla chiusura con download attivi |

### Scheduling

`GET /api/scheduler/status` espone lo stato; `PUT /api/scheduler/pausa`
mette in pausa scheduler e worker lasciando i task in coda. La modifica
dell'intervallo vale dal giro successivo: quello in corso non si interrompe.

### Notifiche (impalcatura)

`notifiche.abilitate` e `notifiche.sorgente` sono persistite e mostrate in
Impostazioni, ma **l'invio non è ancora implementato** (nessun trigger
backend, nessun badge UI). Il plugin Tauri `notification` è presente solo
come base per la feature futura.

---

## series_data.json (export)

Array di oggetti serie. Il DB è la fonte: il JSON viene riscritto a ogni
modifica e serve al binario precedente come fallback di rollback.

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
