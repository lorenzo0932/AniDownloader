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
    "version": "<versione>",
    "port": 8989,
    "downloadRunning": false,
    "sseClients": 0,
    "osNotifyBackend": true
}
```

`osNotifyBackend` è true quando il demone può inviare le notifiche di sistema
(stesso owner del client su Linux con `notify-send`/`gdbus` disponibili). In
tal caso il frontend non le duplica; su macOS/Windows resta al client Tauri.

Questa risposta non ha il campo `success`: è l'unico endpoint informativo
storico che restituisce l'oggetto nudo. `version` è la versione compilata
(`ANIDOWNLOADER_VERSION` in `CMakeLists.txt`).

### `GET /api/scheduler/status` (dal 3.0)

Stato dello scheduler interno (ADR-004).

```json
{
    "success": true,
    "attivo": true,
    "inPausa": false,
    "checkInCoda": 0,
    "ultimoGiro": 1720000000,
    "prossimoGiro": 1720000900,
    "intervalloMinuti": 15,
    "ultimoEsito": "ok",
    "ultimoMessaggio": "giro completato: 2/2 riusciti",
    "pipeline": {"inEsecuzione": false, "interattivoInAttesa": false}
}
```

`pipeline.inEsecuzione` è true mentre gira un check o un run manuale;
`pipeline.interattivoInAttesa` è true quando un run manuale aspetta il turno.
`ultimoEsito` è `ok`, `errore` o vuoto (nessun giro ancora); `prossimoGiro`
è calcolato da `ultimoGiro + intervalloMinuti`.

### `PUT /api/scheduler/pausa` (dal 3.0)

Mette in pausa o riprende scheduler e worker (i task restano in coda).

```json
{
    "pausa": true
}
```

---

### `GET /api/notifications` (dal 3.0)

Notifiche "pronto da guardare": un record per ogni episodio scaricato dal
giro schedulato. Persistono nel DB (tabella `notifications`), quindi il badge
è corretto anche dopo un riavvio del demone.

**Parametri query:**
- `limit` (opzionale) — numero massimo di righe (default 50, tetto 500)

```json
{
    "success": true,
    "unread": 2,
    "items": [
        {"id": 2, "serie": "Naruto", "episodio": 5,
         "creato_il": "2026-11-08T11:18:14.161Z", "letta": false},
        {"id": 1, "serie": "One Piece", "episodio": 1116,
         "creato_il": "2026-11-08T11:18:14.161Z", "letta": false}
    ]
}
```

Le righe sono ordinate per id decrescente (più recenti prima).

### `POST /api/notifications/read` (dal 3.0)

Segna come lette. Con `{"id": N}` solo quella; con body vuoto `{}` tutte.

```json
{"success": true, "unread": 0}
```

### `POST /api/notifications/test` (dal 3.0)

Invia una notifica di sistema di prova dal demone (non persiste nulla e non
dipende da `notifiche.abilitate`). Body opzionale `{"title": "...", "body": "..."}`.

```json
{"success": true, "method": "notify-send", "error": ""}
```

Risponde `503` se nessun notifier è disponibile (es. runtime Flatpak senza
`notify-send`/`gdbus`). Su macOS/Windows il backend non è supportato e resta
il client Tauri.

---

### `GET /api/queue` (dal 3.0)

Vista della coda persistente (tabella `tasks`). Senza `stato` restituisce la
coda attiva (attesa + attivo + fallito); i completati si chiedono con
`?stato=fatto`.

**Parametri query:**
- `stato` (opzionale) — `attesa`, `attivo`, `fallito`, `fatto`
- `limit` (opzionale) — default 100, tetto 500

```json
{
    "success": true,
    "counts": {"attesa": 1, "attivo": 0, "fatto": 12, "fallito": 1},
    "items": [
        {"id": 4, "kind": "check", "serie": "", "episodio": 0, "stato": "attesa",
         "tentativi": 0, "prossima_esecuzione": 0, "priorita": 0,
         "creato_il": "2026-11-08T10:00:00Z", "errore": ""}
    ]
}
```

`prossima_esecuzione` è epoch Unix: `0` = eseguibile subito; un valore futuro
è il backoff del retry. `errore` contiene il motivo leggibile dell'ultimo
fallimento definitivo (vuoto per i task completati o per i fallimenti
precedenti alla migrazione v7).

### `POST /api/queue/retry` (dal 3.0)

"Riprova ora": rimette in `attesa` con scadenza immediata un task fallito (o
già in attesa). I `tentativi` non vengono azzerati, così la storia resta
leggibile.

```json
{"id": 4}
```

```json
{"success": true, "id": 4}
```

---

### `GET /api/storage` (dal 3.0)

Fotografia on-demand dello spazio: disco della serie più pesante, cache
miniature, residui `.aria2`/`.part` e occupazione per serie (ordinate per byte
decrescenti). La scansione legge le cartelle delle serie del repository: nessun
path arbitrario dal client. `freeBytes`/`totalBytes` sono misurati sulla serie
con più byte; senza serie configurate si usa `~`.

```json
{
    "success": true,
    "freeBytes": 536870912000,
    "totalBytes": 1073741824000,
    "cache": {"files": 12, "bytes": 2097152},
    "partial": {"files": 3, "bytes": 1024},
    "series": [
        {"name": "One Piece", "path": "/media/One Piece", "files": 40,
         "bytes": 8589934592, "partialFiles": 0, "partialBytes": 0}
    ]
}
```

### `POST /api/storage/cleanup` (dal 3.0)

Pulizia guidata, due sole azioni sicure: mai file video.

```json
{"azione": "cache"}      // svuota la cache miniature
{"azione": "parziali"}   // rimuove i residui .aria2/.part delle serie
```

```json
{"success": true, "removedFiles": 12, "freedBytes": 2097152}
```

### `GET /api/pipeline` (dal 3.0)

Coda per-episodio della pipeline condivisa: conteggi per fase e job.
Parametri:
- `fase` (opzionale): fase esatta, oppure `problemi` (alias di
  `failed` + `interrupted`).
- `window` (default `tutti`): `recenti` = job non terminali oppure aggiornati
  nelle ultime 24h; `storico` = job terminali aggiornati da più di 24h;
  `tutti` = nessun filtro.
- `run_id` (opzionale): solo i job dell'avvio indicato.
- `offset` (default 0) e `limit` (default 100, tetto 500).

I `counts` restano globali (tutte le fasi, tutti i run).

```json
{
    "success": true,
    "counts": {"queued_download": 1, "downloading": 0,
               "downloaded_waiting_conversion": 0, "converting": 0,
               "completed": 0, "retry_wait": 1, "failed": 0, "interrupted": 0},
    "items": [
        {"id": 1, "run_id": "run-1794153364250-0",
         "job_key": "animeW_scraper:/play/x#1", "serie": "Serie X",
         "episodio": 1, "origine": "interactive", "fase": "queued_download",
         "tentativi": 0, "prossima_esecuzione": 0, "priorita": 100, "errore": "",
         "creato_il": "2026-11-08T10:00:00.000Z",
         "aggiornato_il": "2026-11-08T10:00:00.000Z"}
    ]
}
```

### `POST /api/pipeline/retry` (dal 3.0)

"Riprova" su un job per-episodio: un job `failed` (o già `queued_download`)
torna in coda con scadenza immediata (`prossima_esecuzione = 0`). I
`tentativi` non vengono azzerati e l'errore precedente viene ripulito.

```json
{"id": 1}
```

```json
{"success": true, "id": 1}
```

Risponde `400` se `id` manca o non è un intero, `500` se lo store segnala un
errore di storage. Su un job in una fase non riprovabile la chiamata è
idempotente: nessuna riga cambia e la risposta resta `success`.

---

### Modello Run (schema v6)

Un **Run** è un avvio della pipeline, manuale (`interactive`) o automatico
(`background`). Ogni run è persistito in `pipeline_runs`; ogni job porta il
`run_id` dell'avvio che lo ha prodotto. I job preesistenti alla v6 confluiscono
nel run sintetico `legacy`, che la UI mostra come "Avvio precedente".

Campi del run nelle risposte: `id`, `origine` (`manuale`/`automatico`),
`avviato_il`, `concluso_il` (`null` finché è in corso), `stato`
(`in_corso`, `completato`, `completato_con_errori`, `interrotto`), `totale`,
`completati`, `non_riusciti` (fase `failed`), `saltati`, `in_corso` e
`percent` = `(completati + non_riusciti) / totale * 100`, arrotondato.

### `GET /api/overview` (dal 3.0)

Per la Home: il **run più utile** — in corso; altrimenti il più recente con
episodi (`totale > 0`); altrimenti il più recente — la coda globale e i
problemi aperti. `run` è `null` se non esiste alcun run; `corrente` è il job
attivo (`downloading`/`converting`) o il primo in
`queued_download`/`retry_wait` del run. `tasks` è presente quando `run` non è
`null` ed elenca i task live (anche dei check automatici).

```json
{
    "success": true,
    "run": {"id": "run-1794153364250-0", "origine": "manuale",
            "avviato_il": "2026-11-08T17:03:00.000Z", "concluso_il": null,
            "stato": "in_corso", "totale": 7, "completati": 3,
            "non_riusciti": 1, "saltati": 0, "in_corso": 3, "percent": 57,
            "corrente": {"serie": "Psyren", "episodio": 4, "fase": "converting"},
            "tasks": [
                {"key": "[\"Psyren\",4]", "seriesName": "Psyren", "episode": 4,
                 "phase": "converting", "percent": 42, "detail": "Conversione 42%"}
            ]},
    "coda": {"queued": 2, "downloading": 1, "converting": 1, "retry": 0, "totale": 4},
    "problemi": {"non_riusciti": 1, "interrotti": 0}
}
```

Campi di `tasks[]`: `key` (opaco, coppia serie/episodio), `seriesName`,
`episode`, `phase` (fase osservativa, es. `downloading`, `converting`),
`percent` (`-1` se non misurabile) e `detail` (testo per la UI). La mappa è
popolata sia dal run manuale sia dai check automatici; viene svuotata all'avvio
di un nuovo run/check e ogni voce è rimossa al termine dell'episodio.

Nota: `coda.queued` include anche `downloaded_waiting_conversion`; la somma
dei campi è `coda.totale`. `coda` e `problemi` sono globali (i job non
terminali appartengono comunque al run corrente; i problemi restano visibili
anche dopo la chiusura del run che li ha prodotti).

### `GET /api/runs` (dal 3.0)

Vista "Avvii" di Attività.
Parametri: `window` (`recenti` default | `storico` | `tutti`; 24h),
`origine` (`tutte` default | `manuale` | `automatico`), `stato`
(`tutti` default | `in_corso` | `completato` | `completato_con_errori` |
`interrotto` | `problemi` = ultimi due), `limit` (default 20, tetto 100),
`offset` (default 0).

```json
{
    "success": true,
    "items": [{"id": "run-...", "origine": "automatico",
               "avviato_il": "...", "concluso_il": "...",
               "stato": "completato_con_errori", "totale": 12,
               "completati": 11, "non_riusciti": 1, "saltati": 0,
               "in_corso": 0, "percent": 100}],
    "total": 42,
    "counts": {"in_corso": 1, "recenti": 5, "problemi": 2}
}
```

`counts` è calcolato sulla sola `origine` (ignora `window`/`stato`/`limit`):
`in_corso` = run non conclusi, `recenti` = in corso o conclusi nelle ultime
24h, `problemi` = `completato_con_errori` + `interrotto`.

### `GET /api/runs/:id` (dal 3.0)

Dettaglio di un run con i job in ordine di `id`; `path` è la cartella serie
(per la locandina). `404` se il run non esiste.

```json
{
    "success": true,
    "run": {"id": "run-...", "...": "come sopra"},
    "jobs": [{"id": 12, "run_id": "run-...", "job_key": "...",
              "serie": "Psyren", "episodio": 4, "origine": "interactive",
              "fase": "converting", "tentativi": 0, "prossima_esecuzione": 0,
              "priorita": 100, "errore": "",
              "creato_il": "...", "aggiornato_il": "...",
              "path": "/media/Psyren"}]
}
```

### `POST /api/runs/:id/retry-failed` (dal 3.0)

Rimette in coda (`queued_download`, scadenza immediata) tutti i job `failed`
del run, senza azzerare i tentativi. `404` se il run non esiste.

```json
{"success": true, "retried": 2}
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
            "local_episode_count": 1115,
            "alternate_sources": [...]
        }
    ]
}
```

`identity`: stringa opaca calcolata dal server (servizio + URL canonico).
Il frontend la confronta come stringa, senza normalizzare URL in JS.
Stessa serie su mirror verificato (es. `.so`/`.ac`) ha stessa identity;
stesso titolo con URL diversi resta distinto.

`local_episode_count`: numero di file video presenti nella cartella della
serie, calcolato a ogni richiesta. `_file_index` è l'indice stabile usato
da PUT/DELETE.

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
        "json_file_path": "/home/user/.config/AniDownloader/series_data.json",
        "log_file_path": "/home/user/.cache/AniDownloader/serie_critical_errors.log",
        "is_json_path_customized": false,
        "max_network_retries": 3,
        "retry_delay_ms": 2000,
        "convert_to_h265": true,
        "burst_on_manual_start": true,
        "num_chunks": 0,
        "auto_cleanup_on_close": true,
        "resume_interrupted_downloads": true,
        "scheduling": {"abilitato": true, "intervalloMinuti": 15},
        "notifiche": {"abilitate": false, "sorgente": "entrambi"},
        "pinned_paths": ["/home/user/Video/Anime"]
    }
}
```

La risposta è il contenuto del file `config.json`: possono comparire anche
`show_stop_warning` / `show_close_warning` se la UI li ha scritti.

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

Le chiavi di primo livello sono sostituite in blocco: per aggiornare
`scheduling` o `notifiche` inviare l'oggetto completo, non solo un campo.
Il PUT cancella un'eventuale chiave `output_dir` residua: non governa più
alcun download (la destinazione è `series.path`).

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

Evento notifica (dal 3.0): un episodio è stato scaricato dal giro schedulato.
Non porta `sequence`: la campanella in `App.svelte` lo consuma, la Home lo
ignora (non fa parte dello stato del run manuale).

```
data: {"type":"notification","id":3,"serie":"One Piece","episodio":1116}
```

Il client non deve dedurre nulla dal testo: usa `serie`/`episodio` e ricarica
`GET /api/notifications` se ha perso eventi.

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
