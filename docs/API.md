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
    "version": "2.1.0",
    "port": 8989,
    "downloadRunning": false,
    "sseClients": 0
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

Elenco del contenuto di una directory per il file picker.

**Parametri query:**
- `path` (opzionale) — directory da esplorare (default: `/`; `~` viene espanso)
- `files` (opzionale) — `1` per includere anche i file regolari, non solo le
  directory (default: solo directory)

Le voci che iniziano con `.` sono sempre escluse. L'ordinamento mette le
directory prima dei file, in ordine alfabetico dentro ciascun gruppo.
`parent` è calcolato dal server (nessuno splitting lato client), così il
comando "su" funziona anche su Windows, dove il separatore è `\`.

```json
{
    "success": true,
    "path": "/home/user/Video",
    "parent": "/home/user",
    "entries": [
        {"name": "Anime", "path": "/home/user/Video/Anime", "mtime": 1755000000, "type": "dir", "size": 0},
        {"name": "ep01.mkv", "path": "/home/user/Video/ep01.mkv", "mtime": 1755000001, "type": "file", "size": 734003200}
    ]
}
```

`parent` è `null` alla root (`/` su POSIX, radice del drive su Windows).
Un path inesistente (o non directory) restituisce
`entries` vuota, non un errore.

---

### `GET /api/browse/mounts`

Dischi e volumi montati, ma solo quelli utili all'utente: niente
pseudo-filesystem (`/proc`, `/sys`, `/dev`, `tmpfs`, `overlay`, `squashfs`),
niente path di sistema (`/snap`, `/boot`, `/var/lib/{docker,containers,snapd}`,
`/var/snap`, `/run` tranne `/run/media`) e niente `/home` (coperta dalla
voce "Home" della sidebar). Restano `/` (sempre presente, per uscire da un
mount annidato), i volumi rimovibili (`/media`, `/mnt`, `/run/media`) e i
dischi di rete (FUSE, NFS, CIFS/SMB). Le etichette sono basename leggibili
(`"SSD Sata"`, non `"/run/media/user/SSD Sata"`).

- Linux: `/proc/self/mounts` filtrato (FUSE e NFS restano, sono dischi navigabili)
- macOS: contenuto di `/Volumes`
- Windows: `GetLogicalDrives`, con etichetta per tipo (Disco locale, Rimovibile, Rete, CD/DVD)

```json
{
    "success": true,
    "mounts": [
        {"name": "/", "path": "/"},
        {"name": "Backup", "path": "/mnt/Backup"}
    ]
}
```

---

### `GET /api/browse/places`

Posizioni principali dell'utente per la sidebar del picker (stile file
manager nativo): home per prima, poi le cartelle standard esistenti.
Solo directory esistenti, senza duplicati.

- Linux: legge `~/.config/user-dirs.dirs` (`XDG_*_DIR`); se manca, prova i
  candidati convenzionali (`Desktop`/`Scrivania`, `Documents`/`Documenti`, …)
- macOS: `Desktop`, `Documents`, `Downloads`, `Movies`, `Music`, `Pictures`
- Windows: `Desktop`, `Documents`, `Downloads`, `Music`, `Pictures`, `Videos`
  sotto `%USERPROFILE%`

```json
{
    "success": true,
    "places": [
        {"id": "home", "name": "Home", "path": "/home/user"},
        {"id": "documents", "name": "Documenti", "path": "/home/user/Documenti"}
    ]
}
```

---

### `POST /api/browse/mkdir`

Crea una directory. `name` deve essere un nome singolo: i separatori sono
rifiutati lato server (la composizione del percorso avviene sempre qui).

**Body:** `{"parent": "/home/user/Video", "name": "Anime 2026"}`

```json
{"success": true, "path": "/home/user/Video/Anime 2026"}
```

| Errore | Status | Causa |
|---|---|---|
| `Nome cartella non valido` | 400 | nome vuoto, con `/` o `\`, `.`, `..`, > 255 caratteri |
| `Cartella padre non trovata` | 404 | `parent` non esiste o non è una directory |
| `La cartella esiste gia'` | 409 | il percorso esiste già |
| `Permessi insufficienti` | 403 | |

---

### `POST /api/browse/touch`

Crea un file vuoto. Stesse regole di validazione di `mkdir`.

**Body:** `{"parent": "/home/user/Video", "name": "ep01.mkv"}`

```json
{"success": true, "path": "/home/user/Video/ep01.mkv"}
```

---

### `DELETE /api/browse`

Rimuove un file o una directory. I symlink non vengono mai seguiti: viene
eliminato il link, non il bersaglio.

**Body:** `{"path": "/home/user/Video/Anime", "recursive": false}`

`recursive` è opzionale (default `false`). Senza, una directory non vuota viene
rifiutata **senza essere toccata** e la risposta porta il conteggio degli
elementi, così il client può chiedere una seconda conferma.

```json
{"success": true}
```

| Errore | Status | Causa |
|---|---|---|
| `Percorso non valido` | 400 | `path` assente |
| `Percorso non trovato` | 404 | il path non esiste |
| `La cartella non e' vuota` | 409 | directory con contenuto e `recursive` assente; body: `{"error": ..., "code": 409, "count": 12}` |
| `Rimozione non consentita` | 403 | root, radice di un drive o punto di mount |

La protezione di root, radici dei drive e punti di mount è voluta: il server
è open-access e non deve poter diventare un `rm -rf` su `/`. Il guardrail
conosce TUTTI i punti di mount (anche pseudo-fs e tmpfs, che il picker non
mostra) e scatta prima ancora del check di esistenza, così resta attivo anche
se `stat` fallisce per permessi o voci stantie.

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
        "pinned_paths": ["/home/user/Video/Anime"]
    }
}
```

`pinned_paths` è l'elenco dei path pinnati (stelle) nel file picker: i più
recenti in cima, massimo 20. Un pin su un disco non più montato non viene
rimosso automaticamente: torna nel file picker e mostra l'errore al clic.

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
