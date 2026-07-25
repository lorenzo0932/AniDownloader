# AniDownloader — AGENTS.md

## Build & run

```bash
# Backend C++ (Qt-free, httplib + cpr + nlohmann-json)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Frontend (Svelte 5 SPA)
cd web && npm install && npm run build && cd ..

# Tauri app nativa (desktop)
npx @tauri-apps/cli build    # Produce .deb / .AppImage / .dmg / .msi

# Esecuzione standalone (senza Tauri)
./build/AniDownloader           # CLI mode (silent)
./build/AniDownloader --burst   # CLI mode (massima concorrenza)
./build/AniDownloader --web     # Web UI (porta 8989, auto-incremento)
```

Dipendenze runtime: `aria2c`, `ffmpeg`.
Dipendenze build Tauri (Linux): `webkit2gtk4.1-devel`, `libappindicator-gtk3-devel`.

## Architettura

- **Dual delivery**: stessa UI Svelte 5 accessibile via browser (`--web`) o app nativa (Tauri wrapper).
- **Backend C++17** single-target: `AniDownloader` (httplib, cpr, nlohmann-json, OpenSSL).
- **Namespaces**: `Core::`, `Config::`, `Web::`, `Scrapers::` mappano 1:1 su `include/{core,config,web,scrapers}/`.
- **Headers** in `include/`, implementazioni in `src/`.
- **Entrypoint**: `src/main.cpp` — dispatcher CLI/Web (`--gui` rimosso, vedi branch `legacy/qt-gui`).
- **Scrapers nativi** (nessun Python): `AnimeWScraper`, `AnimeUScraper` ereditano `BaseScraper`.
- **Web backend**: cpp-httplib (header-only), nlohmann-json, OpenSSL::Crypto.
- **Niente autenticazione**: server web open-access (come Transmission/qBittorrent).
- **Qt assente**: la vecchia GUI Qt6 è preservata in `legacy/qt-gui`.

## Branch

| Branch | Scopo |
|--------|-------|
| `main` | Stabile, release |
| `dev` | Integrazione pre-release |
| `feat/tauri` | Migrazione Tauri in corso |
| `legacy/qt-gui` | Vecchia GUI Qt6 (riferimento storico) |

## Configurazione

- `~/.config/AniDownloader/config.json` (auto-generato all'avvio).
- `~/.config/AniDownloader/series_data.json` (dati utente, **gitignorato**).
- `PathHelper` calcola percorsi cross-platform (XDG su Linux, APPDATA su Windows).

## Cosa fare / non fare

- **Nessun test**, nessun linter/formatter/CI. Solo warning CMake standard.
- **`#pragma once`** in tutti gli header.
- **Non committare `series_data.json`** (dati utente, in `.gitignore`).
- **Non committare `web/dist/`** (generato da `npm run build`).
- **Non committare `src-tauri/target/`** (Rust build output, in `.gitignore`).
- **Non committare `src-tauri/binaries/`** (sidecar C++ copiato da CMake, in `.gitignore`).
