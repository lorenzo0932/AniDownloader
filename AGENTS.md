# AniDownloader — AGENTS.md

## Build & run

```bash
# Backend C++ (httplib + cpr + nlohmann-json, C++17)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Frontend (Svelte 5 SPA)
cd web && npm install && npm run build && cd ..

# Tauri app nativa (desktop)
npx @tauri-apps/cli build    # Produce AppImage / .dmg / .msi
```

Dipendenze build C++: `cmake`, `libcurl-devel`, `openssl-devel`.
Dipendenze runtime: `aria2c`, `ffmpeg`.
Dipendenze build Tauri (Linux): `webkit2gtk4.1-devel`, `libappindicator-gtk3-devel`.

### Modalitá di esecuzione

| Flag | Modalitá | Descrizione |
|------|----------|-------------|
| *(nessuno)* | **Silent** | Download sequenziale, risorse minime, solo resoconto finale. Per cron/systemd. |
| `--burst` | **Burst** | Massima concorrenza, dashboard ANSI live, full throughput. Interattivo. |
| `--web` | **Web** | Server HTTP (porta 8989). Usa burst di default. Per Tauri o browser. |
| `--silent` | — | Sopprime stdout in modalitá web. Utile per systemd. |
| `--port N` | — | Porta personalizzata per `--web` (default 8989). |

## Delivery

**Linux**: AppImage (formato primario, universale) + tar.gz headless (`anidownloaderd`).

**Windows**: `.exe` (NSIS installer).

**macOS**: `.dmg`.

Tutti gli artefatti generati da GitHub Actions su tag `v*` e caricati su Releases.

### install.sh (utente)

```bash
./install.sh              # Menu interattivo, download da GitHub Releases
./install.sh v2.0.0       # Versione specifica
```

Opzioni: 1) Solo Desktop (AppImage)  2) Solo Headless (daemon)  3) Entrambi (default)

Se GitHub non é raggiungibile, fa build locale.

### Installer headless (senza Tauri)

```bash
./install.sh              # Opzione 2 (Headless)
# oppure:
tar xzf anidownloaderd-*.tar.gz
./anidownloaderd --web    # Server HTTP sulla 8989
./anidownloaderd --silent # Nessun output stdout
```

## Architettura

- **Dual delivery**: stessa UI Svelte 5 via browser (`--web`) o app nativa (Tauri wrapper).
- **Backend C++17** single-target: `AniDownloader` (httplib, cpr, nlohmann-json, OpenSSL).
- **Modalitá silent** (nessun flag): risorse minime, download sequenziale, nessuna dashboard. Pensata per servizio headless/automatico.
- **Modalitá burst** (`--burst`): sfrutta tutta la banda/CPU, dashboard ANSI live.
- **Web server** (`--web`): usa burst di default (perché Tauri/web UI = utente attivo). Il frontend Svelte manda `{"burst": true}`.
- **Namespaces**: `Core::`, `Config::`, `Web::`, `Scrapers::` mappano 1:1 su `include/{core,config,web,scrapers}/`.
- **Headers** in `include/`, implementazioni in `src/`.
- **Entrypoint**: `src/main.cpp` — dispatcher CLI/Web.
- **Scrapers nativi**: `AnimeWScraper`, `AnimeUScraper` ereditano `BaseScraper`.
- **Web backend**: cpp-httplib (header-only), nlohmann-json, OpenSSL::Crypto.
- **Niente autenticazione**: server web open-access (come Transmission/qBittorrent).
- **Qt preservato** in `legacy/qt-gui`.

## Branch

| Branch | Scopo |
|--------|-------|
| `main` | Stabile, release |
| `dev` | Integrazione pre-release |
| `feat/tauri` | Migrazione Tauri in corso |
| `legacy/qt-gui` | Vecchia GUI Qt6 (riferimento storico) |

## CI/CD

- **GitHub Actions** su `.github/workflows/release.yml`.
- Trigger: push di tag `v*`.
- Produce: AppImage (Linux), tar.gz headless (Linux), .exe (Windows), .dmg (macOS).
- Carica automaticamente su GitHub Releases.

## Configurazione

- `~/.config/AniDownloader/config.json` (auto-generato all'avvio).
- `~/.config/AniDownloader/series_data.json` (dati utente, **gitignorato**).
- `PathHelper` calcola percorsi cross-platform (XDG su Linux, APPDATA su Windows).

## Cosa fare / non fare

- **`#pragma once`** in tutti gli header.
- **Non committare `series_data.json`** (dati utente, in `.gitignore`).
- **Non committare `web/dist/`** (generato da `npm run build`).
- **Non committare `src-tauri/target/`** (Rust build output, in `.gitignore`).
- **Non committare `src-tauri/binaries/`** (sidecar C++ copiato da CMake, in `.gitignore`).
