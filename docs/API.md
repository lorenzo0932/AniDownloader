# API REST — Riferimento

Il server HTTP (`--web`) espone API REST su `http://localhost:8989`.
Il frontend Svelte comunica esclusivamente tramite queste API.

## Formato richiesta/risposta

- Content-Type: `application/json`
- CORS: Access-Control-Allow-Origin: `*`

Risposta di successo:

```json
{"success": true, ...}
```

Risposta di errore:

```json
{"error": "messaggio", "code": 400}
```

---

## Endpoint

### `GET /api/status`

Stato dell'applicazione.

```json
{
    "success": true,
    "version": "2.0.0",
    "config_path": "/home/user/.config/AniDownloader/config.json",
    "series_path": "/home/user/.config/AniDownloader/series_data.json",
    "log_path": "/home/user/.config/AniDownloader/logs/AniDownloader.log"
}
```

---

### `GET /api/series`

Lista delle serie.

**Parametri query:**
- `sort` (opzionale) — campo per ordinamento: `name`, `last_downloaded_at`, `added`, `local_episode_count`, `continue`
- `dir` (opzionale) — direzione: `asc`, `desc`

```json
{
    "success": true,
    "series": [
        {
            "_file_index": 0,
            "name": "One Piece",
            "path": "~/Video/Anime/One Piece",
            "series_page_url": "https://animeworld.so/...",
            "service": "animeW_scraper",
            "continue": true,
            "is_high_priority": false,
            "passed_episodes": 1100,
            "last_downloaded_at": "2026-07-28T15:30:00Z",
            "last_downloaded_episode": 1115,
            "alternate_sources": [...]
        }
    ]
}
```

---

### `POST /api/series`

Aggiunge una nuova serie.

```json
{
    "name": "One Piece",
    "path": "~/Video/Anime/One Piece",
    "series_page_url": "https://animeworld.so/...",
    "service": "animeW_scraper",
    "continue": true,
    "is_high_priority": false,
    "passed_episodes": 0,
    "alternate_sources": []
}
```

Risposta:

```json
{"success": true}
```

---

### `PUT /api/series/:index`

Modifica la serie all'indice `index`.

Corpo: stesso formato di POST. Risposta:

```json
{"success": true}
```

---

### `DELETE /api/series/:index`

Rimuove la serie all'indice `index`.

Risposta:

```json
{"success": true}
```

---

### `POST /api/series/fetch-name`

Dato un URL, recupera il nome della serie dal sito.

```json
{"url": "https://animeworld.so/..."}
```

Risposta:

```json
{"success": true, "name": "One Piece"}
```

---

### `GET /api/series/:index/description`

Descrizione NFO della serie.

```json
{"success": true, "description": "Testo della descrizione..."}
```

---

### `GET /api/series/:index/poster`

URL del poster della serie.

```json
{"success": true, "poster_path": "/path/to/poster.jpg"}
```

---

### `GET /api/poster`

Restituisce l'immagine del poster.

**Parametri query:**
- `path` — percorso assoluto del file poster

Risposta: binary image con Content-Type appropriato.

---

### `GET /api/description`

Restituisce la descrizione NFO testuale.

**Parametri query:**
- `path` — percorso della cartella serie

Risposta:

```json
{"success": true, "description": "..."}
```

---

### `GET /api/browse`

Elenco delle directory del filesystem per il browser cartelle.

**Parametri query:**
- `path` (opzionale) — directory da esplorare (default: `~`)

```json
{
    "success": true,
    "entries": ["Cartella1", "Cartella2", ...],
    "current_path": "/home/user"
}
```

---

### `POST /api/browse/pick`

Seleziona una directory e restituisce il path assoluto.

```json
{"path": "/home/user/Video/Anime"}
```

Risposta:

```json
{"success": true, "resolved": "/home/user/Video/Anime"}
```

---

### `GET /api/config`

Legge la configurazione corrente.

```json
{
    "success": true,
    "config": {
        "max_network_retries": 3,
        "retry_delay_ms": 2000,
        "convert_to_h265": true,
        "num_chunks": 0,
        "auto_cleanup_on_close": true,
        "chromedriver_path": "chromedriver"
    }
}
```

---

### `PUT /api/config`

Aggiorna la configurazione.

```json
{
    "max_network_retries": 5,
    "convert_to_h265": false
}
```

Risposta:

```json
{"success": true}
```

---

### `POST /api/download/start`

Avvia i download.

```json
{"burst": true}
```

Risposta:

```json
{"success": true}
```

---

### `POST /api/download/stop`

Ferma i download in corso.

```json
{"success": true}
```

---

### `GET /api/download/status`

Stato del download engine.

```json
{
    "success": true,
    "running": true
}
```

---

### `GET /api/download/events`

SSE (Server-Sent Events) per aggiornamenti live.

```
event: progress
data: {"type":"progress","series":"One Piece","message":"Analisi...","episode":0}

event: progress
data: {"type":"progress","series":"One Piece","message":"DL 45%","episode":1}

event: progress
data: {"type":"progress","series":"One Piece","message":"Conv 67%","episode":1}

event: finished
data: {"type":"finished","series":"One Piece","episode":1,"success":true,"dlTime":12.5,"convTime":34.2}

event: skipped
data: {"type":"skipped","series":"One Piece","reason":"Già aggiornata"}

event: done
data: {"type":"done"}
```

Il client si connette con `EventSource` nativo del browser.

**Campi evento progress:**
- `series` — nome della serie
- `message` — messaggio di stato (`"Analisi..."`, `"DL 45%"`, `"Conv 50%"`, `"MODE:DL"`, ecc.)
- `episode` — numero episodio (0 durante analisi, >0 in download/conversione)

**Campi evento finished:**
- `series` — nome della serie
- `episode` — numero episodio
- `success` — `true`/`false`
- `dlTime` — secondi di download
- `convTime` — secondi di conversione
- `error` — messaggio errore (se success=false)

**Campi evento skipped:**
- `series` — nome della serie
- `reason` — motivo dello skip

---

### `GET /api/log`

Legge le ultime righe del log.

**Parametri query:**
- `lines` (default 100) — numero di righe

```json
{
    "success": true,
    "lines": ["[2026-07-28 15:30:00] INFO: ...", ...]
}
```
