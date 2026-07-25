# AniDownloader — AGENTS.md

## Build & run

```bash
# Backend (richiede Qt6, libcurl-dev, OpenSSL di sistema)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Frontend (Svelte 5 SPA)
cd web && npm install && npm run build && cd ..

# Esecuzione
./build/AniDownloader           # CLI mode (silent)
./build/AniDownloader --burst   # CLI mode (massima concorrenza)
./build/AniDownloader --gui     # GUI Qt6
./build/AniDownloader --web     # Web UI (porta 8989, auto‑incremento se occupata)
```

Dipendenze runtime obbligatorie: `aria2c`, `ffmpeg`.

## Architettura

- **Single CMake target**: `AniDownloader` (C++17, Qt6 Widgets).
- **Namespaces**: `Core::`, `Config::`, `Gui::`, `Web::` mappano 1:1 con `include/{core,config,gui,scrapers,web}/`.
- **Headers** in `include/`, **implementazioni** in `src/`.
- **Entrypoint**: `src/main.cpp` — dispatcher GUI/CLI/Web basato su flag `--gui` / `--web`.
- **Scrapers nativi** (nessun Python): `AnimeWScraper`, `AnimeUScraper` ereditano `BaseScraper`.
- **Logger** thread-safe centralizzato (`Core::Logger`).
- **AUTOMOC ON** — niente file `.moc` separati.
- **Web backend**: cpp-httplib (header‑only), nlohmann-json, OpenSSL::Crypto.
- **Crypto**: solo JWT (HMAC-SHA256, inutilizzato ma disponibile).
- **Niente autenticazione**: server web open-access (come Transmission/qBittorrent).

## Configurazione

- `~/.config/AniDownloader/config.json` (auto-generato all'avvio).
- `~/.config/AniDownloader/series_data.json` (dati utente, **gitignorato**).
- `PathHelper` calcola percorsi cross-platform (XDG su Linux, APPDATA su Windows).

## Endpoint API Web

Tutti gli endpoint sono **open access** (nessuna autenticazione richiesta).

| Metodo | Path | Descrizione |
|--------|------|-------------|
| GET | `/api/status` | Stato server |
| GET/POST/PUT/DELETE | `/api/series[/:id]` | CRUD serie (file JSON plaintext) |
| GET/PUT | `/api/config` | Config (file JSON plaintext) |
| POST | `/api/download/start` | Avvia download |
| POST | `/api/download/stop` | Ferma download |
| GET | `/api/download/status` | Stato download |
| GET | `/api/download/events` | SSE (progresso live) |
| GET | `/api/log` | Log server |

## Cosa fare / non fare

- **Nessun test**, nessun linter/formatter/CI configurati. Solo warning CMake standard.
- **Non scrivere in `output/`** — staging vuoto non usato dal build. `build/` è l'unica directory di output valida.
- **`#pragma once`** in tutti gli header tranne `MainWindow.hpp` (`#ifndef` classico).
- **Non committare `series_data.json`** (dati utente, in `.gitignore`).
- **Non committare il frontend Svelte in `web/dist/`** (non ancora creato).
- Branch attivo: `feature/web_app`. Remote: `lorenzo0932/AniDownloader.git`.
