# AniDownloader — AGENTS.md

Documentazione di progetto. Ogni modifica a questo file è un commit `docs:`
diretto su `dev` (eccezione housekeeping "docs un-file").

## Build & run

```bash
# Backend C++ (httplib + cpr + nlohmann-json, C++20)
cmake --preset dev                    # Ninja + ccache (sviluppo veloce)
cmake --build --preset dev            # NOTA: preset già pinnati a -j16

# Fallback senza Ninja/ccache
cmake --preset dev-slow && cmake --build --preset dev-slow

# Frontend (Svelte 5, embedded nel binario C++)
cd web && npm install && npm run build && cd ..

# Test (mirati durante lo sviluppo)
ctest --test-dir build -R test_core/test_media/test_scraper
ctest --test-dir build -R smoke_e2e   # e2e (serve lato backend)
```

Dipendenze build C++: `cmake>=3.21`, compilatore C++20 (GCC 13+, Clang 14+,
MSVC 2022 17.6+ — servono `std::format`/`std::jthread`), `libcurl-devel`,
`openssl-devel`, `ccache` (preset dev). Runtime: `aria2c`, `ffmpeg`.
Tauri (desktop): `webkit2gtk-4.1-devel`. Flatpak (build locale):
`flatpak-builder`.

### Regole build (macchina locale da 32 core, RAM != infinita)

- **MAI `cmake --build -j$(nproc)`**: OOM sicuro con link LTO. Max `-j16`
  (già impostato dai preset), scendere a `-j4` sotto pressione.
- Prima di build pesanti: `free -h`. `/tmp` è tmpfs = RAM: niente file grossi
  lì dentro senza controllare.
- Dopo un OOM: verificare processi orfani (`ps aux --sort=-%mem`) e spazio `/tmp`.

### Modalitá di esecuzione

| Flag | Modalitá | Descrizione |
|------|----------|-------------|
| *(nessuno)* | **Silent** | Download sequenziale, risorse minime, solo resoconto finale. Per cron/systemd. |
| `--burst` | **Burst** | Massima concorrenza, dashboard ANSI live, full throughput. Interattivo. |
| `--web` | **Web** | Server HTTP (porta 8989). Usa burst di default. Per Tauri o browser. |
| `--silent` | — | Sopprime stdout in modalitá web. Utile per systemd. |
| `--port N` | — | Porta personalizzata per `--web` (default 8989). |

## Delivery

**Linux desktop**: **Flatpak** `com.anidownloader.desktop` (canale unico —
AppImage deprecata). **Linux headless**: tar.gz `anidownloaderd-*.tar.gz`.
**Windows**: `.exe` (NSIS installer). **macOS**: `.dmg`.

Artefatti generati da GitHub Actions su tag `v*` e caricati su Releases.

### install.sh (utente)

```bash
./install.sh              # Menu interattivo, download da GitHub Releases
./install.sh v2.0.0       # Versione specifica
```

Opzioni: 1) Solo Desktop (Flatpak)  2) Solo Headless (daemon + systemd)  3) Entrambi (default)
Se GitHub non é raggiungibile, fa build locale (`--local`; Flatpak solo x86_64).

## Architettura

- **Dual delivery**: stessa UI Svelte 5 via browser (`--web`) o app nativa
  (Tauri v2 wrapper). Frontend embedded nel binario: un solo file
  `.service`/daemon, zero dipendenze runtime.
- **Backend C++20** single-target: `AniDownloader` (httplib, cpr,
  nlohmann-json, OpenSSL).
- **Modalitá silent** (nessun flag): pipeline sequenziale, niente dashboard.
  Pensata per servizio headless/automatico.
- **Modalitá burst** (`--burst`): tutta la banda/CPU, dashboard ANSI live.
- **Web server** (`--web`): usa burst di default (Tauri/web UI = utente
  attivo). Il frontend Svelte manda `{"burst": true}`.
- **Namespaces**: `Core::`, `Config::`, `Web::`, `Scrapers::` mappano 1:1 su
  `include/{core,config,web,scrapers}/`; implementazioni in `src/`.
- **Entrypoint**: `src/main.cpp` — dispatcher CLI/Web.
- **Scrapers nativi**: `AnimeWScraper`, `AnimeUScraper` ereditano `BaseScraper`.
- **Tauri v2**: wrappa il backend come **sidecar** (`anidownloaderd`, copiato
  da CMake in `src-tauri/binaries/`) e serve il frontend embedded.
- **Niente autenticazione**: server web open-access (come Transmission/qBittorrent).
- La GUI Qt storica vive solo su `origin/legacy/qt-gui` (riferimento, non in dev).

## Test

- `test_core`, `test_media`, `test_scraper` (CTest) + `smoke_e2e` (bash).
- Verifica mirata durante lo sviluppo (`ctest -R <target>`, `bash -n`); suite
  completa solo a fine feature.

## Branch

| Branch | Scopo |
|--------|-------|
| `main` | Stabile, release |
| `dev` | Integrazione pre-release — **base dei branch di feature/fix** |

### Workflow feature (regola rigida)

Ogni feature/fix vive su un branch dedicato con **piano effimero**:
1. `git checkout -b <tipo>/<slug>` da `dev`.
2. Piano in `plan/<slug>.md` (può essere generato con `/plan`): **mai
   committato né pushato** — è il punto di rientro se la feature non finisce.
3. Implementazione con commit locali (solo codice, mai piano/artefatti AI).
4. A fine implementazione: `/verify` + review dell'utente.
5. **Push del branch e merge su `dev` SOLO su richiesta esplicita** (mai
   automatici). A merge riuscito: delete della branch + eliminazione del file
   piano. Niente pull/PR/fetch automatici.

Commit diretti su `dev` SOLO per micro-correzioni di housekeeping (una riga,
un commento, un path) o modifiche docs un-file (es. questo AGENTS.md) — sempre
come commit locali, il push resta su richiesta dell'utente.

## Artefatti AI — mai in git

- `plan/`, `bugs_analysis.md`, `.opencode/`, `scripts/bundle_codebase.sh`:
  gitignored, in locale. Mai committati/pushati.
- L'**AGENTS.md** di repo è documentazione di progetto e vive su `dev`.

## CI/CD

- **`ci.yml`**: matrix OS (Linux/macOS) — build C++ + ctest + lint frontend.
- **`release.yml`**: tag `v*` o `workflow_dispatch` (scelta piattaforma) —
  build C++ → sidecar in `src-tauri/binaries/` → packaging (headless tar.gz,
  NSIS, .dmg; il desktop Linux è buildato da `flatpak.yml`) → GitHub Releases.
- **`flatpak.yml`**: bundle Flatpak (Tauri + sidecar C++ embedded) su `v*`.

## Configurazione

- `~/.config/AniDownloader/config.json` (auto-generato all'avvio).
- `~/.config/AniDownloader/series_data.json` (dati utente, **gitignorato**).
- `PathHelper` calcola percorsi cross-platform (XDG su Linux, APPDATA su Windows).

## Sessioni opencode — economia di contesto

- **File grandi (>400 righe, es. `smoke.sh`, `WebServer.cpp`)**: niente
  riletture integrali ripetute — grep mirato + `read` con offset/limit.
- **1 feature = 1 sessione**: riassunto breve (commits + stato), poi sessione
  nuova.
- Verifica mirata invece della suite completa a ogni passo.

## Cosa fare / non fare

- **`#pragma once`** in tutti gli header.
- **Licenza**: progetto MIT (`LICENSE`, copyright Lorenzo Ammatuna);
  `licenses/` contiene i testi integrali delle licenze terze parti
  (aggiornarla quando cambia una dipendenza) e `THIRD_PARTY_NOTICES.md` la
  tabella riassuntiva.
- **Non committare**: `series_data.json`, `web/dist/`, `src-tauri/target/`,
  `src-tauri/binaries/`, artefatti `.flatpak`/AppImage, `plan/`, `.opencode/`.