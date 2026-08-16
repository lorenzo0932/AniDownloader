# AniDownloader

> Scarica, converti e organizza automaticamente i tuoi anime preferiti.

Sei stanco di controllare manualmente se è uscito il nuovo episodio?
AniDownloader è un demone scritto in **C++20** che controlla i siti di streaming italiani
(AnimeWorld, AnimeUnity), scarica i nuovi episodi via **aria2c**, li converte in
**H.265 (HEVC)** risparmiando fino al 50% di spazio, e li organizza automaticamente.

Niente Python. Niente browser. Solo C++ nativo, veloce ed efficiente.

<div align="center">

---

## Perché un altro downloader di anime?

L'ecosistema italiano dello streaming amatoriale è frammentato e in continuo
cambiamento. I tool esistenti sono spesso script Python fragili, dipendono da
browser automation pesante (Selenium/Playwright), o semplicemente non supportano
la conversione H.265.

AniDownloader nasce per risolvere questi problemi:

- **Scrapers nativi C++** — niente Python, niente browser. Solo HTTP + regex
  nativi, leggeri e veloci.
- **Conversione H.265 automatica** — FFmpeg con chunking parallelo per
  sfruttare tutti i core della CPU. Codec video moderno, spazio dimezzato.
- **Dual delivery** — stessa UI Svelte 5 accessibile via browser (`--web`)
  o come app nativa con tray icon (Tauri). Scegli tu.
- **Silent mode** — nessuna output, perfetto per cron/systemd su un server
  o NAS. Il resoconto finale è l'unica cosa che vedi.
- **Burst mode** — massima concorrenza, dashboard ANSI live, tutto il throughput
  possibile. Per quando vuoi guardare la barra di progresso.

---

## Funzionalità

|                                      |                                                                                                                                              |
| ------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------- |
| 🎯**Scrapers nativi**          | AnimeWorld (`AnimeWScraper`), AnimeUnity (`AnimeUScraper`). C++ puro, leggero, senza dipendenze esterne.                              |
| ⚡**Download parallelo**       | Fino a 16 connessioni per file con aria2c. Concorrenza multi-serie configurabile automaticamente in base alla CPU.                           |
| 🎞️**HEVC automatico**        | Conversione post-download con FFmpeg. Uso intelligente di`/dev/shm` se la RAM è sufficiente. Chunking parallelo per encoding più veloce. |
| 🌐**Web UI moderna**           | Svelte 5 SPA con dashboard live in tempo reale (SSE), gestione serie CRUD, tema dark/light, log viewer.                                      |
| 🖥️**App nativa**             | Tauri v2: tray icon, notifiche desktop, finestra dedicata. Packaging AppImage / NSIS (.exe) / .dmg.                                       |
| 🔄**Aggiornamenti automatici** | Check versioni via GitHub API con confronto semver.                                                                                          |
| 🔔**Notifiche desktop**        | Completamento, errori e skip segnalati nativamente dal sistema.                                                                              |
| 🕐**Automazione systemd**      | Timer per controllo periodico (default ogni 15 minuti). Servizio web permanente.                                                             |
| 💾**Ripresa crash**            | Rilevamento file parziali (.aria2 residui), verifica integrità con ffprobe, cleanup automatico.                                             |
| 📦**Singolo binario**          | Frontend embedded nel C++: un solo file`.service` o `.AppImage`, zero dipendenze runtime.                                                |

---

## Installazione

> Tutti i pacchetti pronti all'uso sono pubblicati su
> **[GitHub Releases](https://github.com/lorenzo0932/AniDownloader/releases/latest)** —
> nessuna compilazione richiesta.

### Utenti

**Linux (x86_64)** — installazione guidata con un comando:

```bash
curl -fsSL https://raw.githubusercontent.com/lorenzo0932/AniDownloader/main/install.sh | bash
```

Il menu ti guida nella scelta:

1. **Solo Desktop** (AppImage + launcher nel menu applicazioni)
2. **Solo Headless** (daemon systemd per server/NAS)
3. **Entrambi** (consigliato)

In alternativa scarica l'AppImage `AniDownloader-<versione>-linux-x86_64.AppImage`
dalle Releases, rendila eseguibile (`chmod +x`) e aprila con doppio click.

**Windows** — scarica `AniDownloader_<versione>_x64-setup.exe` dalle Releases ed
eseguilo con doppio click (installer NSIS, senza privilegi di amministratore).

**macOS (Apple Silicon)** — scarica `AniDownloader_<versione>_aarch64.dmg` dalle
Releases, aprilo e trascina l'app nella cartella Applicazioni.

> ⚠️ **Architetture supportate**: i pacchetti precompilati sono solo **x86_64**
> su Linux e **arm64** su macOS. Su dispositivi **ARM Linux** (Raspberry Pi, NAS)
> non esiste download: puoi compilare da sorgente solo il daemon headless con
> `./install.sh --local` (opzione 2). Il Desktop (Tauri/AppImage) su Linux ARM
> e i **Mac Intel** non sono supportati.

### Disinstallazione

```bash
# Linux
./uninstall.sh

# Windows
.\uninstall.ps1
```

Gli script chiedono se mantenere la configurazione prima di rimuovere i file.

### Sviluppatori

Build da sorgente:

```bash
# Linux — build locale forzata (utile anche su ARM per il solo headless)
./install.sh --local

# Windows
.\install.ps1

# macOS
npm run tauri:build   # tauri-cli pinnato in package.json (riproducibile)
```

Documentazione completa: [docs/BUILD.md](docs/BUILD.md).

---

## Utilizzo

### Web UI (raccomandata)

```bash
anidownloaderd --web
# Web UI: http://localhost:8989
```

Accessibile da qualsiasi browser sulla rete locale. Interfaccia completa con
dashboard, gestione serie, configurazione e log.

### Porta personalizzata

```bash
anidownloaderd --web --port 9090
```

### Modalità CLI

```bash
anidownloaderd              # Normale (silenziosa, sequenziale)
anidownloaderd --burst      # Massima concorrenza + dashboard ANSI
```

### Automazione systemd

L'installer configura automaticamente:

- `anidownloaderd.service` — server web persistente
- `anidownloader-check.timer` — controllo nuovi episodi ogni 15 minuti

```bash
journalctl --user -u anidownloaderd.service -f
```

---

## Screenshot

> *(Aggiungi qui screenshot della Web UI, della dashboard CLI, del tray icon)*

---

## Requisiti

### Runtime

- **aria2c** — download accelerato
- **ffmpeg** — conversione e verifica video

```bash
# Debian/Ubuntu
sudo apt install ffmpeg aria2

# Fedora
sudo dnf install ffmpeg aria2

# Arch Linux
sudo pacman -S ffmpeg aria2
```

### Build

- **CMake** >= 3.17
- **Compilatore C++20** (GCC 13+, Clang 14+, MSVC 2022 17.6+ — servono `std::format`/`std::jthread`)
- **Node.js** >= 18
- **OpenSSL** / **libcurl** (gestite via FetchContent o di sistema)

---

## Compilazione da sorgente

```bash
git clone https://github.com/lorenzo0932/AniDownloader.git
cd AniDownloader

# 1. Frontend
cd web && npm install && npm run build && cd ..

# 2. Backend
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# (Opzionale) App nativa Tauri (tauri-cli pinnato in package.json)
npm run tauri:build
```

Per lo sviluppo quotidiano il preset `dev` usa **Ninja + ccache** (build
incrementali ~1s anche dopo il touch di header inclusi ovunque):

```bash
cmake --preset dev && cmake --build --preset dev
```

Se ninja o ccache non sono disponibili, fallback classico
`cmake --preset dev-slow`.

Vedi [docs/BUILD.md](docs/BUILD.md) per dettagli su opzioni CMake, cross-compilazione e troubleshooting.

---

## Configurazione

La configurazione è in `~/.config/AniDownloader/config.json`. Usa l'interfaccia
Web (pagine "Impostazioni") per modificarla senza rischi.

### Esempio `series_data.json`

```json
[
    {
        "name": "One Piece",
        "path": "~/Video/Anime/One Piece",
        "series_page_url": "https://animeworld.so/...",
        "service": "animeW_scraper",
        "continue": true,
        "passed_episodes": 1100,
        "alternate_sources": [
            {
                "service": "animeU_scraper",
                "series_page_url": "https://animeunity.to/..."
            }
        ]
    }
]
```

Vedi [docs/CONFIGURAZIONE.md](docs/CONFIGURAZIONE.md) per la reference completa
di tutti i campi.

---

## Architettura (per sviluppatori)

```
AniDownloader_dev/
├── include/               # Header C++
│   ├── core/              # Engine, processor, logger, probe
│   ├── config/            # Config manager, path helper
│   ├── scrapers/          # Scraper base, AnimeW, AnimeU
│   └── web/               # WebServer, Crypto, embedded frontend
├── src/                   # Implementazioni C++
├── web/                   # Frontend Svelte 5 SPA
├── src-tauri/             # Wrapper Tauri v2 (Rust)
├── scripts/               # embed_web.py (frontend → binario)
├── docs/                  # Documentazione tecnica
├── install.sh / .ps1      # Installer cross-platform
└── CMakeLists.txt         # Build system
```

Approfondimenti: [docs/ARCHITETTURA.md](docs/ARCHITETTURA.md).

---

## Sviluppo

- **Branch**: `main` (release), `dev` (integrazione pre-release, base dei branch di feature/fix). Convenzione: ogni funzionalità/fix in `feat/<num>-<slug>` / `fix/<slug>` creato da `dev`, merge squash in `dev`, branch eliminato dopo il merge; niente branch per housekeeping/CI/docs (commit diretti su dev). I piani e gli artefatti AI vivono solo in locale (`local/reference`, mai pushato).
- **Commit**: `tipo(area): messaggio` — es. `feat(core): parallel planning`, `fix(web): sse reconnect`
- **Aree**: `core` (C++ engine), `web` (Svelte), `tauri` (desktop), `config`, `scrapers`

### Supporto AI

Questo progetto è sviluppato con **opencode** e modelli di AI come assistenti
alla programmazione. Ogni decisione architetturale e linea di codice è
revisionata e approvata manualmente.

---

## Roadmap

### Fatto in 2.0

- [X] Riscrittura completa in C++20
- [X] Web UI Svelte 5 (dashboard, CRUD, config, log)
- [X] API REST + SSE progresso live
- [X] App nativa Tauri (tray, notifiche, finestra)
- [X] Download parallelo multi-thread
- [X] Conversione H.265 automatica
- [X] Scrapers nativi (AnimeWorld, AnimeUnity)
- [X] Installazione/disinstallazione cross-platform
- [X] Retry automatico (HTTP, aria2c)
- [X] Logger con rotazione
- [X] Cache poster immagini
- [X] Aggiornamenti automatici (GitHub API)

### In arrivo

- [ ] Fallback automatico su fonti alternative
- [ ] Editor UI per fonti alternative
- [ ] Supporto più servizi di streaming

---

## Licenza

AniDownloader è rilasciato sotto licenza **MIT** — vedi il file
[`LICENSE`](LICENSE). Puoi usare, modificare e ridistribuire il codice
liberamente, anche in progetti commerciali, purché vengano mantenute le
note di copyright e permesso.

L'applicazione integra componenti di terze parti (cpp-httplib, cpr,
nlohmann-json, Tauri, Svelte, Vite, ...): le loro licenze e attribuzioni
sono elencate in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), con
i testi integrali nella cartella [`licenses/`](licenses/). I tool runtime
(aria2, FFmpeg) non sono distribuiti con l'app: vengono installati
dall'utente e mantengono le proprie licenze.
