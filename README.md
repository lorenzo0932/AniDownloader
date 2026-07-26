# AniDownloader 2.0

Una riscrittura completa in **C++17** del sistema AniDownloader. Massima velocità, efficienza e integrazione nativa con Linux e Windows.

UI: **Web Interface** (Svelte 5 SPA) con wrapper nativo Tauri (AppImage/.dmg/.msi).

---

## Funzionalità

* **Web UI moderna** (Svelte 5): dashboard download real-time con SSE, gestione serie CRUD con griglia di card e poster, configurazione temi dark/light, log viewer. Accessibile da browser locale o remoto.
* **App Nativa Tauri**: tray icon, notifiche desktop, finestra dedicata. Packaging AppImage (Linux), .dmg (macOS), .msi (Windows).
* **Download Parallelo ad alte prestazioni**: `ExecutionEngine` multi-thread con controllo granulare della concorrenza.
* **Conversione Automatica H.265**: Trascodifica HEVC post-download per risparmiare spazio (~50%).
* **Scrapers Nativi**: Parser C++ per AnimeWorld e AnimeUnity. Nessuna dipendenza Python.
* **Retry di Rete Automatico**: Exponential backoff su errori HTTP, aria2c e ChromeDriver.
* **Notifiche Desktop**: Native per completamento, errori e skip.
* **Aggiornamenti Automatici**: GitHub API con confronto semver.
* **Gestione Serie Avanzata**: Multi-stagione, numerazione continua, episodi passati, fonti multiple.
* **Monitoraggio Real-Time**: Progresso live via SSE (Web UI).
* **Automazione Systemd**: Servizio `.service` e `.timer` per esecuzione periodica.
* **Installazione Cross-Platform**: Script per Linux e Windows.

<div align="center">

<!-- [PLACEHOLDER: GIF della gestione serie] -->
<!-- Inserire qui la GIF che mostra la vista Gestione Serie con tabella, anteprima e bottoni -->

</div>

---

## Requisiti di Sistema

### Dipendenze Runtime

* **aria2c**: Download accelerato e parallelo.
* **ffmpeg**: Conversione e verifica integrità video.

```bash
# Debian/Ubuntu
sudo apt install ffmpeg aria2

# Fedora
sudo dnf install ffmpeg aria2

# macOS (Homebrew)
brew install ffmpeg aria2

# Windows (Winget)
winget install "FFmpeg (Essentials Build)"
winget install aria2
```

### Dipendenze di Build (solo per compilazione da sorgente)

* **CMake** >= 3.17
* **Compilatore C++17** (GCC, Clang, MSVC)
* **Node.js** >= 18 (per build frontend Web UI)
* **Ninja** (consigliato, opzionale)
* **OpenSSL** / **libcurl** (gestite automaticamente via FetchContent)

Le librerie **nlohmann_json**, **cpr** e **cpp-httplib** sono scaricate automaticamente durante il build tramite CMake FetchContent.

---

## Installazione

### Linux

```bash
chmod +x install.sh
./install.sh
```

Lo script compila il progetto, installa l'eseguibile in `~/.local/bin/`, configura icone e file `.desktop`, e abilita il timer systemd per i controlli automatici.

### Windows

```powershell
.\install.ps1
```

Installa l'eseguibile in `%LOCALAPPDATA%\AniDownloader` e crea un shortcut nel menu Start.

### Disinstallazione

```bash
# Linux
chmod +x uninstall.sh
./uninstall.sh

# Windows
.\uninstall.ps1
```

Gli script di disinstallazione chiedono se mantenere la configurazione prima di rimuovere i file.

---

## Compilazione da Sorgente

```bash
git clone https://github.com/lorenzo0932/AniDownloader.git
cd AniDownloader

# 1. Build frontend Web UI (Svelte 5)
cd web && npm install && npm run build && cd ..

# 2. Build backend C++
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

L'eseguibile e il frontend compilato (`build/frontend/`) vengono generati automaticamente.

---

## Configurazione

La configurazione è gestita tramite il file `series_data.json`. È fortemente consigliato usare l'interfaccia grafica ("Gestione Serie") per evitare errori di sintassi.

All'avvio, l'applicazione crea automaticamente i file di configurazione necessari.

### Esempio

```json
[
    {
        "name": "Nome Serie",
        "path": "/percorso/di/destinazione",
        "series_page_url": "https://animeworld.so/...",
        "service": "animeW_scraper",
        "continue": false,
        "passed_episodes": 0,
        "alternate_sources": [
            {
                "service": "animeU_scraper",
                "series_page_url": "https://animeunity.to/..."
            }
        ]
    }
]
```

| Campo | Tipo | Descrizione |
|-------|------|-------------|
| `name` | string | Nome visualizzato della serie |
| `path` | string | Cartella di destinazione per gli episodi |
| `series_page_url` | string | URL della pagina serie sul sito di streaming |
| `service` | string | Scraper da usare: `animeW_scraper` o `animeU_scraper` |
| `continue` | bool | `true` se la serie continua da una stagione precedente |
| `passed_episodes` | int | Numero di episodi già visti (richiesto se `continue` è `true`) |
| `alternate_sources` | array | Fonti alternative per la stessa serie (opzionale) |
| `alternate_sources[].service` | string | Scraper della fonte alternativa |
| `alternate_sources[].series_page_url` | string | URL della pagina serie sulla fonte alternativa |

### Configurazione Applicazione

Il file di configurazione globale (`config.json`) supporta i seguenti campi aggiuntivi:

| Campo | Tipo | Default | Descrizione |
|-------|------|---------|-------------|
| `max_network_retries` | int | `3` | Numero massimo di tentativi retry per HTTP/aria2c |
| `retry_delay_ms` | int | `2000` | Delay iniziale tra retry (raddoppia ad ogni tentativo) |
| `convert_to_h265` | bool | `true` | Abilita conversione H.265 post-download |
| `num_chunks` | int | `0` | Chunk FFmpeg (0 = modalita auto dinamica) |
| `auto_cleanup_on_close` | bool | `true` | Pulisce file parziali alla chiusura |

---

## Utilizzo

### Web UI (Raccomandata)

```bash
./build/AniDownloader --web
# Web UI: http://localhost:8989
```

Accessibile da qualsiasi browser sulla rete locale. Interfaccia completa con dashboard download in tempo reale, gestione serie, configurazione e log.

### Web UI (Porta Personalizzata)

```bash
./build/AniDownloader --web --port 9090
```

### Modalità CLI

```bash
./build/AniDownloader                # Normale
./build/AniDownloader --burst        # Massima concorrenza
```

### Automazione Systemd (Linux)

```bash
# L'installer genera ed abilita automaticamente i servizi:
./install.sh          # Scegli "Headless" o "Entrambi"

# Manualmente, se preferisci:
anidownloaderd --web --silent &
```

---

## Struttura del Progetto

```text
AniDownloader_dev/
├── include/                    # Header C++ (.hpp)
│   ├── core/                   # ExecutionEngine, MediaProcessor, Logger, MediaProbe
│   ├── config/                 # AppConfigManager, PathHelper
│   ├── scrapers/               # AnimeWScraper, AnimeUScraper, ScraperUtils
│   └── web/                    # WebServer, CryptoUtils (JWT)
├── src/                        # Implementazioni (.cpp)
│   ├── core/                   # Logica di processing e gestione dati
│   ├── config/                 # Configurazione e percorsi
│   ├── scrapers/               # Parser nativi per i servizi
│   ├── web/                    # HTTP server (httplib), SSE, API REST
│   └── main.cpp                # Entry point (--web, --burst, silent)
├── web/                        # Frontend Svelte 5 SPA
│   ├── src/                    # Componenti (StatusPage, DashboardPage, ConfigPage, LogsPage)
│   ├── lib/                    # API client, tema, auth
│   └── package.json            # Dipendenze Node.js
├── src-tauri/                  # Wrapper Tauri v2 (AppImage/.dmg/.msi)
│   ├── src/                    # main.rs (sidecar launch, tray icon)
│   ├── icons/                  # Icone applicazione
│   ├── capabilities/           # Permessi Tauri
│   └── tauri.conf.json         # Configurazione Tauri
├── resources/                  # Logo applicazione
├── scripts/                    # Utility di build (embed_web.py)
├── install.sh                  # Installatore Linux
├── install.ps1                 # Installatore Windows
├── uninstall.sh                # Disinstallatore Linux
└── CMakeLists.txt              # Sistema di build
```

---

## Roadmap

### Completato in 2.0

- [x] Riscrittura completa in C++17
- [x] Web UI (Svelte 5 SPA) con dashboard, gestione serie, configurazione, log
- [x] API REST con SSE per progresso live
- [x] App Nativa Tauri (tray icon, notifiche, finestra dedicata)
- [x] Download parallelo multi-thread
- [x] Conversione H.265 automatica
- [x] Scrapers nativi (AnimeWorld, AnimeUnity)
- [x] Compatibilita Windows (installazione, percorsi, User-Agent)
- [x] Logger con rotazione file
- [x] Cache immagini poster
- [x] Installazione e disinstallazione cross-platform
- [x] Retry di rete automatico (HTTP, aria2c, ChromeDriver)
- [x] Notifiche desktop (completamento, errori, skip)
- [x] Aggiornamenti automatici (GitHub API, semver)
- [x] Struttura dati fonti multiple (alternate_sources)

### Funzionalita Future

- [ ] Fallback automatico sulle fonti alternative
- [ ] Interfaccia editor per fonti alternative nella UI

---

## Monitoraggio

```bash
# Log del servizio systemd (headless)
journalctl --user -u anidownloaderd.service -f

# Log interno dell'applicazione
# Si trovano in ~/.config/AniDownloader/logs/
```

<div align="center">

<!-- [PLACEHOLDER: Screenshot dei log e monitoraggio] -->

</div>

---

## Sviluppo

Il progetto segue un approccio **monorepo trunk-based**:

- **Branch attivo**: `feat/tauri` — contiene backend C++, frontend Svelte e wrapper Tauri
- **Feature branch temporanei** per lavori consistenti (es. `feat/search-refactor`)
- **Convenzione commit**: `tipo(area): messaggio` — `feat(core):`, `fix(web):`, `chore:`, `docs:`

Aree: `core` (C++ engine), `web` (Svelte), `tauri` (desktop wrapper), `config`/`scrapers` per i rispettivi moduli C++.

---

## Licenza

Questo progetto è distribuito sotto licenza personale. Consulta il file LICENSE per i dettagli.
