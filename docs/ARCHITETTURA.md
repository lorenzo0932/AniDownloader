# Architettura di AniDownloader

## Visione d'insieme

AniDownloader è un sistema **monolitico C++20** che integra:

- Motore di download/conversione multi-thread
- Scrapers nativi per siti streaming italiani
- Server HTTP (REST + SSE) per UI web
- Frontend Svelte 5 embedded nel binario
- Wrapper desktop Tauri v2 (Rust)

```
┌─────────────────────────────────────────────────────┐
│                   main.cpp                           │
│  CLI dispatcher: --web / --burst / (silent)          │
└─────────┬─────────────────────────────────┬──────────┘
          │ --web                            │ CLI
          ▼                                  ▼
┌─────────────────────┐       ┌─────────────────────────┐
│   Web::WebServer    │       │  Core::ExecutionEngine   │
│  (cpp-httplib)      │       │  (multi-thread worker)   │
│                     │       │                          │
│  ┌───────────────┐  │       │  ┌──────────────────┐   │
│  │ SSE broadcast │  │       │  │ MediaProcessor   │   │
│  │ live progress │  │       │  │ per serie/task   │   │
│  └───────────────┘  │       │  └────────┬─────────┘   │
│                     │       │           │               │
│  Embedded Svelte 5  │       │           ▼               │
│  frontend (.html)   │       │  ┌──────────────────┐   │
└─────────────────────┘       │  │ PlanningService  │   │
                              │  │ → Scraper        │   │
                              │  └──────────────────┘   │
                              └─────────────────────────┘
```

## Namespace

| Namespace | Ruolo | Header | Source |
|-----------|-------|--------|--------|
| `Core` | Motore principale, download, conversione, log | `include/core/*.hpp` | `src/core/*.cpp` |
| `Config` | Gestione configurazione, percorsi | `include/config/*.hpp` | `src/config/*.cpp` |
| `Web` | Server HTTP, frontend, JWT | `include/web/*.hpp` | `src/web/*.cpp` |
| `Scrapers` | Scraper astratti e concreti | `include/scrapers/*.hpp` | `src/scrapers/*.cpp` |

## Flusso dati (CLI mode)

```
1. main.cpp → SeriesRepository::loadSeriesData()
                ↓
2. ExecutionEngine::run()
    ├── per ogni serie: thread separato
    │   └── PlanningService::planSingleSeries()
    │       └── Scraper::planSeriesTask()
    │           ├── AnimeW: lista episodi + endpoint statico /api/episode/info
    │           ├── ScraperUtils::computeNextNeeded()
    │           └── restituisce DownloadTask[]
    │
    ├── sorting per priorità
    │
    └── thread pool (maxConcurrentTasks × 2)
        └── MediaProcessor::processTask()
            ├── MediaProbe:: → check esistente
            ├── aria2c → download
            ├── MediaProbe:: → verifica
            └── [se H265] ffmpeg → conversione
```

## Flusso dati (Web mode)

```
1. main.cpp → Web::WebServer::start()
2. httplib server in ascolto su :8989
3. Frontend Svelte connesso via EventSource (SSE)
4. POST /api/download/start → runDownloads()
    └── stesso flusso CLI, con callback SSE
5. Ogni progress/result → broadcastSseEvent()
6. Frontend aggiorna dashboard in tempo reale
```

## Dipendenze C++

| Libreria | Versione | Uso |
|----------|----------|-----|
| nlohmann/json | 3.11.3 | JSON parsing/serializzazione |
| cpr (libcpr) | 1.10.5 | HTTP requests (scrapers, GitHub API) |
| cpp-httplib | 0.18.1 | Server HTTP/SSE |
| OpenSSL::Crypto | (sistema) | AES-256-GCM, PBKDF2 per JWT |
| Threads | (sistema) | parallelismo |

## File embedding

Il frontend Svelte 5 viene compilato in `web/dist/` e poi convertito in
array C da `scripts/embed_web.py` che genera:

- `include/web/embedded_web.hpp` — header con dichiarazioni extern + lookup map
- `src/web/embedded_web_data.cpp` — array di byte dei file frontend

Questo permette di distribuire **un singolo binario** senza dipendenze
frontend esterne.

## Cross-platform

- `Config::PathHelper` astrae i path (XDG su Linux, APPDATA su Windows)
- `ScraperUtils::Q()` quoting shell differente per POSIX/Windows
- `ScraperUtils::DEVNULL()` redirezione output nulla cross-platform
