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

### `GET /api/scheduler/status` (dal 3.0)

Stato dello scheduler interno (ADR-004).

```json
{
    "success": true,
    "attivo": true,
    "inPausa": false,
    "checkInCoda": 0,
    "ultimoGiro": 1720000000
}
```

### `PUT /api/scheduler/pausa` (dal 3.0)

Mette in pausa o riprende scheduler e worker (i task restano in coda).

```json
{
    "pausa": true
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
            "identity": "animeW_scraper:/play/one-piece.HASH",
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

`identity`: stringa opaca calcolata dal server (servizio + URL canonico).
Il frontend la confronta come stringa, senza normalizzare URL in JS.
Stessa serie su mirror verificato (es. `.so`/`.ac`) ha stessa identity;
stesso titolo con URL diversi resta distinto.

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
    "alternate_sources": [],
    "fetch_metadata": true
}
```

`fetch_metadata` (opzionale, default `true`): con URL AnimeWorld scarica
una tantum `folder.jpg` (locandina HD) e `tvshow.nfo` (trama, generi, anno,
stato) nella cartella serie — solo se non esistono già, mai sovrascritture.
Best-effort: se fallisce, l'aggiunta riesce comunque.

Risposta (successo):

```json
{"success": true, "index": 3, "identity": "animeW_scraper:/play/one-piece.HASH"}
```

Duplicato (stessa serie già in gestione, anche da mirror verificato):

```json
{"error": "Serie già in gestione", "code": 409, "error_code": "series_already_managed", "identity": "animeW_scraper:/play/one-piece.HASH"}
```

`code` resta numerico (allineato allo status HTTP); il motivo applicativo è
in `error_code`. Il controllo è atomico nel repository: due richieste
concorrenti sullo stesso repository non creano due copie. La garanzia vale
per thread dello stesso processo; due daemon sullo stesso JSON restano
fuori garanzia (lock interprocesso via `exec.lock` per i giri, non per CRUD).

---

### `PUT /api/series/:index`

Modifica la serie all'indice `index`.

Corpo: stesso formato di POST. Risposta (successo):

```json
{"success": true, "identity": "animeW_scraper:/play/one-piece.HASH"}
```

Il cambio URL verso una serie già gestita da un altro indice è rifiutato
con lo stesso 409 `series_already_managed` (nessuna modifica applicata).

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

Restituisce la miniatura WebP del poster (generata con Lanczos via ffmpeg,
cache su disco), oppure l'originale come fallback.

**Parametri query:**
- `path` — percorso della cartella serie
- `w` — tier di larghezza px, arrotondato al superiore tra
  `32/96/480/720/1080` (default `480`; mai upscale oltre il sorgente)
- `q` — qualità WebP 1..100 (default `80`; fuori range → default)

Risposta: binary image (`image/webp`, oppure `image/jpeg` in fallback) con
`Cache-Control: public, max-age=31536000, immutable`. La chiave cache
contiene mtime+size del sorgente: se il poster cambia, cambia la chiave —
mai miniature stantii. Se il poster manca: placeholder SVG 220×320.

### `GET /api/cache/thumbs`

Info sulla cache miniature su disco.

Risposta:

```json
{ "success": true, "files": 12, "bytes": 412030 }
```

### `DELETE /api/cache/thumbs`

Svuota la cache miniature (si autoriduce oltre i 500 MB con eviction LRU;
le voci rimosse vengono rigenerate al bisogno).

Risposta:

```json
{ "success": true, "removedFiles": 12, "freedBytes": 412030 }
```

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

### `GET /api/discover` (dal 3.0)

Pagina Scopri: ricerca e filtri sul catalogo AnimeWorld, una pagina del sito
(~40 card) alla volta. Con `keyword` interroga `/search` (full-text
server-side), altrimenti `/filter` (i due modi del sito sono separati: se
c'è la keyword i filtri vengono ignorati).

**Parametri query:**
- `keyword` (opzionale) — testo libero, max 100 caratteri
- `genre` (ripetibile) — id genere, es. `?genre=1&genre=4` (la forma
  `?genre=1,4` il sito la ignora: zero risultati)
- `genre_mode` — `and`/`or` (solo con più generi)
- `season` — `winter|spring|summer|fall|unknown`
- `year` — es. `2026`
- `type` — 0 Anime, 4 Movie, 1 OVA, 2 ONA, 3 Special, 5 Music
- `status` — 0 In corso, 1 Finito, 2 Non rilasciato, 3 Droppato
- `studio` — testo libero (es. `MAPPA`)
- `language` — `jp|it|ch|kr|en|fr`
- `dub` — `1` solo doppiati
- `sort` — 0 Standard, 1 Ultime aggiunte, 2 A-Z, 3 Z-A, 4 Più vecchi,
  5 Più recenti, 6 Più visti, 7 Meglio valutati
- `page` — pagina da leggere (default 1, max 500)

Risposta:

```json
{
    "success": true, "page": 1, "maxPage": 122,
    "items": [
        {"titolo": "Naruto Shippuden", "pageUrl": "/play/naruto-shippuden.v3U8a",
         "posterUrl": "https://img.animeworld.ac/locandine/01ec....png", "tooltipId": "193",
         "identity": "animeW_scraper:/play/naruto-shippuden.v3U8a"}
    ]
}
```

Ogni item espone `identity` (stessa regola di `GET /api/series`): il badge
"In gestione" si decide confrontando stringhe, senza parsing URL nel browser.

I poster sono URL diretti (`img.animeworld.ac`): il frontend li carica senza proxy.

---

### `GET /api/discover/tooltip` (dal 3.0)

Dettaglio lazy di una card Scopri (trama, voto, stato, generi).

**Parametri query:**
- `id` — id numerico del tooltip (campo `tooltipId` di `/api/discover`)

Risposta:

```json
{
    "success": true, "trama": "...", "voto": "8.5", "stato": "Finito",
    "dataUscita": "01 Gennaio 2020", "nomiAlternativi": "...",
    "generi": ["Azione", "Avventura"]
}
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
  candidati convenzionali (`Desktop`/`Scrivania`, `Documents`/`Documenti`, …).
  Le voci puntate alla home stessa (`XDG_DESKTOP_DIR="$HOME/"`) sono
  disabilitate da spec e vengono saltate, non mostrate come doppione di Home.
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

Avvia i download. Il corpo è opzionale: la UI non lo invia e la modalità
arriva dalla config `burst_on_manual_start` (default `true`); un `burst`
esplicito nel corpo vince per quella richiesta.

```json
{"burst": true}
```

Risposta:

```json
{"success": true, "message": "Download started"}
```

---

### `POST /api/download/stop`

Ferma i download in corso.

```json
{"success": true}
```

---

### `GET /api/download/status`

Snapshot osservativo del run manuale avviato dalla Home. `running` resta
disponibile; i campi aggiuntivi permettono di ricostruire la UI dopo reload o
riconnessione. Risposta `Cache-Control: no-store`.

```json
{
    "success": true,
    "running": true,
    "runId": 1,
    "sequence": 42,
    "phase": "processing",
    "status": "Elaborazione in corso",
    "stopped": false,
    "tasks": [
        {"key": "[\"One Piece\",1]", "seriesName": "One Piece", "episode": 1,
         "phase": "waiting_conversion", "percent": -1,
         "detail": "Download completo · attesa di uno slot", "dlTime": 0.0, "convTime": 0.0}
    ],
    "events": []
}
```

`key` è opaca per il client; identifica una coppia serie/episodio nel run,
non sostituisce l'identità canonica del repository. `percent=-1` significa
assenza di una percentuale misurabile: non visualizzare una barra fittizia.
Le percentuali di download e conversione sono indipendenti.

Fasi episodio: `queued`, `analysing` (episodio 0), `downloading`,
`verifying_download`, `waiting_conversion`, `converting`, `finalizing`,
`completed`, `error`, `interrupted`, `skipped`. Il download concluso passa
alla verifica, quindi all'attesa conversione senza rimanere al 99%.
Se non serve conversione passa direttamente al completamento dopo la verifica.

`events` conserva al massimo 120 passaggi significativi, con `id`, `timestamp`
(millisecondi Unix), `type`, `series`, `episode` e, quando presente, `task`.
Non contiene ogni aggiornamento percentuale. Task ed eventi restano in memoria
fino al run successivo o al riavvio del demone; non sono una coda persistente.
Non rappresentano i singoli episodi del giro background dello scheduler.

---

### `GET /api/download/events`

SSE (Server-Sent Events) per aggiornamenti live.

```
data: {"type":"started","runId":1,"sequence":1,"timestamp":1792300000000}

data: {"type":"progress","series":"One Piece","message":"Trasferimento in corso","episode":1,"phase":"downloading","percent":45,"runId":1,"sequence":42,"timestamp":1792300001000,"task":{"key":"[\"One Piece\",1]","seriesName":"One Piece","episode":1,"phase":"downloading","percent":45,"detail":"Trasferimento in corso","dlTime":0.0,"convTime":0.0}}

```

Gli eventi `finished`, `skipped` e `done` mantengono i campi precedenti e
ricevono anch'essi `runId`, `sequence`, `timestamp`; esempio dei campi base:

```
data: {"type":"finished","series":"One Piece","episode":1,"success":true,"dlTime":12.5,"convTime":34.2}

data: {"type":"skipped","series":"One Piece","reason":"Già aggiornata"}

data: {"type":"done","running":false,"stopped":false}
```

Il trasporto usa righe `data:`, non eventi SSE nominati: leggere `onmessage`
e discriminare con `type`. Snapshot e SSE condividono un ordine `sequence`
globale nel processo. Il client bufferizza gli eventi durante la richiesta
snapshot e applica soltanto quelli con sequenza successiva; al reconnect o
a un salto di sequenza rilegge lo snapshot. Al riavvio del demone le sequenze
ripartono: sostituire lo stato alla nuova connessione, non confrontarlo con
quello del processo precedente.

Gli eventi che modificano un episodio includono `task`, la sua proiezione
aggiornata; quelli significativi includono `log`, da aggiungere alla cronologia.
`started` azzera il run precedente. `done.stopped=true` non è un successo:
gli episodi non terminali diventano `interrupted`. `finished.interrupted`
distingue uno stop da un errore. Il campo `message` è solo presentazione:
mai dedurre la fase da testi, percentuali nel testo, emoji o marker `MODE:*`.

Eventi coda (dal 3.0, scheduler interno):

```
data: {"type":"queue","evento":"avviato","task":{"id":1,"kind":"check","serie":"","episodio":0,"tentativi":0}}
data: {"type":"queue","evento":"completato","task":{...}}
data: {"type":"queue","evento":"riprova","task":{...}}
data: {"type":"queue","evento":"fallito","task":{...}}
```

Il client si connette con `EventSource` nativo del browser.

**Campi evento progress:**
- `series` — nome della serie
- `message` — messaggio di presentazione, non un protocollo da interpretare
- `episode` — numero episodio (0 durante analisi, >0 in download/conversione)
- `phase`, `percent`, `detail` — fase e progresso strutturati (`detail` per episodi)
- `task` — proiezione aggiornata dell'episodio, inclusa anche in `finished`/`skipped`

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
