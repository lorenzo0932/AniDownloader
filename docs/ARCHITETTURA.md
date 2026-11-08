# Architettura di AniDownloader

## Visione d'insieme

AniDownloader è un sistema **monolitico C++20** che integra:

- Motore di download/conversione multi-thread
- Scheduler interno + coda persistente su SQLite (ADR-004)
- Scrapers nativi per siti streaming italiani
- Server HTTP (REST + SSE) per UI web
- Frontend Svelte 5 embedded nel binario
- Wrapper desktop Tauri v2 (Rust) con il backend come sidecar

```
┌──────────────────────────────────────────────────────────────┐
│                        main.cpp                              │
│     CLI dispatcher: --web / --burst / (silent)               │
└───────────┬──────────────────────────────────────┬───────────┘
            │ --web                                │ CLI
            ▼                                      ▼
┌───────────────────────────┐        ┌─────────────────────────┐
│     Web::WebServer        │        │  Core::CheckRunner      │
│     (cpp-httplib)         │        │  (CLI: un giro)         │
│                           │        │   └ ExecutionEngine     │
│  REST + SSE broadcast     │        │      ├ PlanningService  │
│  Embedded Svelte 5        │        │      └ MediaProcessor   │
│                           │        │  aria2c + ffmpeg        │
│  Core::Scheduler          │        └─────────────────────────┘
│   └ WorkerPool (coda)     │
│      └ CheckRunner        │
└───────────┬───────────────┘
            ▼
┌──────────────────────────────────────────────────────────────┐
│           SQLite (fonte autorevole) + JSON (export/fallback)  │
│  series_data.db: series, tasks, kv, migrations                │
└──────────────────────────────────────────────────────────────┘
```

## Namespace

| Namespace | Ruolo | Header | Source |
|-----------|-------|--------|--------|
| `Core` | Motore, scheduler, coda, DB, download, conversione, log, scraper | `include/core/*.hpp`, `include/scrapers/*.hpp` | `src/core/*.cpp`, `src/scrapers/*.cpp` |
| `Config` | Gestione configurazione, percorsi | `include/config/*.hpp` | `src/config/*.cpp` |
| `Web` | Server HTTP, SSE, frontend embedded | `include/web/*.hpp` | `src/web/*.cpp` |

Le classi scraper (`BaseScraper`, `AnimeWScraper`, `AnimeUScraper`) vivono in
`namespace Core`, non in un namespace separato.

## Flusso dati (CLI mode)

```
1. main.cpp → CheckRunner::runOnce()
                ↓ (InstanceLock, repository o JSON)
2. SeriesRepository::snapshotSeriesData()  (DB, fallback JSON)
                ↓
3. ExecutionEngine::run()
    ├── per ogni serie: planning parallelo
    │   └── PlanningService::planSingleSeries()
    │       └── Scraper::planSeriesTask()
    │           ├── AnimeW: lista episodi + endpoint statico /api/episode/info
    │           ├── ScraperUtils::computeNextNeeded()
    │           └── restituisce DownloadTask[]
    ├── sorting per priorità
    └── worker pool (maxConcurrentTasks × 2)
        └── MediaProcessor::processTask()
            ├── MediaProbe → check esistente
            ├── aria2c → download
            ├── MediaProbe → verifica
            └── [se H265] ffmpeg → conversione
```

## Flusso dati (Web mode)

```
1. main.cpp → Web::WebServer::start()
2. httplib in ascolto su :8989
3. Frontend Svelte connesso via EventSource (SSE)
4. Scheduler interno → QueueStore (SQLite) → WorkerPool
     └── handler "check" → CheckRunner (giro di controllo serie)
5. POST /api/download/start → run manuale (ExecutionEngine) con callback SSE
6. Ogni progress/result/evento coda → broadcastSseEvent()
7. Frontend aggiorna Home, Gestione e log in tempo reale
```

Run manuale e scheduler sono **due percorsi d'ingresso distinti**, ma nel
demone esiste **un solo esecutore**: `Core::PipelineGate` (pipeline condivisa,
fase 1) serializza check periodico e run manuale, con precedenza al secondo.
La coda per-episodio condivisa fra le due origini è persistita in
`pipeline_jobs`; ogni avvio è un **Run** (sotto).

## Run (schema v6)

Un **Run** è un avvio della pipeline — manuale (origine `interactive`) o
automatico (`background`) — ed è l'unità che Home e Attività leggono.

- Tabella `pipeline_runs`: `id` (`run-<epoch_ms>-<seq>`), `origine`,
  `avviato_il`, `concluso_il` (NULL finché in corso), `stato` (`in_corso`,
  `completato`, `completato_con_errori`, `interrotto`), `saltati`.
- `pipeline_jobs.run_id` collega ogni job al suo avvio; indice
  `(run_id, fase)`. I conteggi del run non sono contatori: si derivano dai job
  (`completed`, `failed`, `interrupted`, non terminali), quindi non possono
  divergere.
- `ExecutionEngine::runPipeline` chiude i run rimasti aperti
  (`closeOrphanRuns`), apre il run (`beginRun`), passa il `run_id` a ogni
  `enqueue`, incrementa `saltati` per le serie già aggiornate e chiude sempre
  il run con un guard RAII: `interrotto` su stop/eccezione, altrimenti
  `completato_con_errori` se ci sono job `failed`/`interrupted`, altrimenti
  `completato`.
- I job preesistenti alla v6 finiscono nel run sintetico `legacy` (etichetta
  UI "Avvio precedente"), con stato `completato_con_errori` se avevano
  fallimenti, altrimenti `completato`.
- API: `GET /api/overview` (ultimo run + coda + problemi),
  `GET /api/runs` (finestra/origine/stato, paginato), `GET /api/runs/{id}`
  (dettaglio job), `POST /api/runs/{id}/retry-failed`. `/api/pipeline` accetta
  `run_id`, `offset`, `fase=problemi` e `window=recenti|storico|tutti`.

## Persistenza

- `series_data.db` — SQLite vendored (`third_party/sqlite`), fonte
  autorevole. Tabelle: `series`, `tasks`, `kv`, `migrations`, `notifications`,
  `pipeline_jobs`, `pipeline_runs`.
- `series_data.json` — export della tabella `series`, riscritto a ogni
  modifica; serve come fallback di rollback per il binario precedente.
- `SeriesRepository` è l'unico punto di mutazione: cache invalidata su
  (mtime,size) del DB, mutazioni atomiche anti-duplicato.

## Dipendenze C++

| Libreria | Versione | Uso |
|----------|----------|-----|
| nlohmann/json | 3.11.3 | JSON parsing/serializzazione |
| cpr (libcpr) | 1.10.5 | HTTP requests (scrapers, GitHub API) |
| cpp-httplib | 0.18.1 | Server HTTP/SSE |
| SQLite | amalgamation vendored | Persistenza serie e coda |
| Threads | (sistema) | parallelismo |

TLS/HTTPS è fornito da libcurl (via cpr); non c'è autenticazione né JWT.

## File embedding

Il frontend Svelte 5 viene compilato in `web/dist/` e poi convertito in
array C da `scripts/embed_web.py` che genera:

- `include/web/embedded_web.hpp` — header con dichiarazioni extern + lookup map
- `src/web/embedded_web_data.cpp` — array di byte dei file frontend

Questo permette di distribuire **un singolo binario** senza dipendenze
frontend esterne.

## Cross-platform

- `Config::PathHelper` astrae i path (XDG su Linux, APPDATA su Windows)- `ScraperUtils::Q()` quoting shell differente per POSIX/Windows
- `ScraperUtils::DEVNULL()` redirezione output nulla cross-platform
- Autostart: systemd (Linux), Task Scheduler (Windows), LaunchAgent (macOS)
