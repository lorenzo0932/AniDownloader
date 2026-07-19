# AniDownloader 2.0

Una riscrittura completa in **C++17** con **Qt6** del sistema AniDownloader. Massima velocità, efficienza e integrazione nativa con Linux e Windows.

<div align="center">

<!-- [PLACEHOLDER: GIF principale dell'interfaccia 2.0] -->
<!-- Inserire qui la GIF che mostra il flusso completo: planning → download parallelo → conversione -->

*L'interfaccia moderna basata su Qt6 con supporto DPIAware, temi dark/light e controllo completo dei download.*

</div>

---

## Funzionalità

* **Interfaccia Grafica Nativa Qt6**: GUI moderna con temi dark/light, scroll fluidi e controlli reattivi. Supporto completo per qualsiasi risoluzione e dimensione del font di sistema (DPI-aware).
* **Download Parallelo ad alte prestazioni**: `ExecutionEngine` multi-thread con controllo granulare della concorrenza. Satura la CPU e la rete senza bloccare l'interfaccia.
* **Conversione Automatica H.265**: Trascodifica HEVC post-download per risparmiare spazio (~50%) mantenendo la qualità video.
* **Scrapers Nativi**: Parser C++ per AnimeWorld e AnimeUnity. Nessuna dipendenza Python.
* **Retry di Rete Automatico**: Riprova automatica con exponential backoff su errori HTTP, fallimenti di download aria2c e sessioni ChromeDriver. Configurabile da file config.
* **Notifiche Desktop**: Balloon notification native per completamento, errori e skip. Il tray icon informa dello stato senza dover tener aperta la finestra.
* **Aggiornamenti Automatici**: Controllo all'avvio tramite GitHub API con confronto versione semver. Notifica l'utente con link diretto alla release.
* **Multiple Fonti di Download**: Struttura dati pronta per fonti alternate per la stessa serie (campo JSON `alternate_sources`).
* **Gestione Serie Avanzata**: Supporto per serie multi-stagione con numerazione continua e episodi passati.
* **Monitoraggio Real-Time**: Barre di progresso per serie, stato dettagliato nel log, indicatore RAM.
* **Tray Icon**: Controllo rapido dall'icona di sistema.
* **Drag & Drop**: Trascina URL per aggiungere serie o file JSON per importare l'elenco.
* **Dual Mode (GUI & CLI)**: Interfaccia grafica o esecuzione da terminale/script.
* **Automazione Systemd**: File `.service` e `.timer` pronti per l'uso su Linux.
* **Installazione Cross-Platform**: Script `install.sh` (Linux) e `install.ps1` (Windows) con configurazione automatica.

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
* **Qt6** (Widgets)
* **Ninja** (consigliato, opzionale)
* **OpenSSL** / **libcurl** (gestite automaticamente via FetchContent)

Le librerie **nlohmann_json** e **cpr** sono scaricate automaticamente durante il build tramite CMake FetchContent.

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
git clone https://github.com/lorenzoAni/AniDownloader.git
cd AniDownloader
git checkout dev

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

L'eseguibile verrà generato nella cartella `build/`.

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

### Modalità Grafica (Raccomandata)

```bash
./build/AniDownloader --gui
```

<div align="center">

<!-- [PLACEHOLDER: GIF dello stop sicuro] -->
<!-- Inserire qui la GIF che mostra la funzionalità di stop con conferma -->

*Stop sicuro con conferma e pulizia automatica dei file parziali.*

</div>

### Modalità CLI

```bash
./build/AniDownloader
./build/AniDownloader --burst  # Modalità burst (massima concorrenza)
```

### Automazione Systemd (Linux)

```bash
mkdir -p ~/.config/systemd/user/
cp systemd_services/AniDownloader.service ~/.config/systemd/user/
cp systemd_services/AniDownloader.timer ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now AniDownloader.timer
```

Il servizio esegue controlli automatici ogni 15 minuti.

---

## Struttura del Progetto

```text
AniDownloader_dev/
├── include/                    # Header C++ (.hpp)
│   ├── core/                   # ExecutionEngine, MediaProcessor, Logger, MediaProbe
│   ├── gui/                    # MainWindow, Dialogs, Widgets, ScaleHelper, Styles
│   ├── config/                 # AppConfigManager, PathHelper
│   └── scrapers/               # AnimeWScraper, AnimeUScraper, ScraperUtils
├── src/                        # Implementazioni (.cpp)
│   ├── core/                   # Logica di processing e gestione dati
│   ├── gui/                    # UI, segnali/slot, tema dinamico
│   ├── config/                 # Configurazione e percorsi
│   ├── scrapers/               # Parser nativi per i servizi
│   └── main.cpp                # Entry point (GUI + CLI)
├── resources/                  # Logo applicazione
├── systemd_services/           # Automazione Linux (.service, .timer)
├── legacy_python/              # Versione Python originale (riferimento storico)
├── install.sh                  # Installatore Linux
├── install.ps1                 # Installatore Windows
├── uninstall.sh                # Disinstallatore Linux
├── uninstall.ps1               # Disinstallatore Windows
└── CMakeLists.txt              # Sistema di build
```

---

## Roadmap

### Completato in 2.0

- [x] Riscrittura completa in C++17 con Qt6
- [x] GUI nativa con temi dark/light
- [x] Download parallelo multi-thread
- [x] Conversione H.265 automatica
- [x] Scrapers nativi (AnimeWorld, AnimeUnity)
- [x] Compatibilita Windows (installazione, percorsi, User-Agent)
- [x] DPI-aware scaling (font e dimensioni basati sul sistema)
- [x] Tray icon e controlli rapidi
- [x] Drag & Drop (URL e JSON)
- [x] Logger con rotazione file
- [x] Cache immagini poster
- [x] Installazione e disinstallazione cross-platform
- [x] Retry di rete automatico (HTTP, aria2c, ChromeDriver)
- [x] Notifiche desktop (completamento, errori, skip)
- [x] Aggiornamenti automatici (GitHub API, semver)
- [x] Struttura dati fonti multiple (alternate_sources)

### Funzionalita Future

- [ ] GIF di presentazione: demo interattive delle funzionalita principali per il README
- [ ] Fallback automatico sulle fonti alternative
- [ ] Interfaccia editor per fonti alternative nella GUI

---

## Monitoraggio

```bash
# Log del servizio systemd
journalctl --user -u AniDownloader.service -f

# Log interno dell'applicazione
# Disponibile nella vista Download della GUI
```

<div align="center">

<!-- [PLACEHOLDER: Screenshot dei log e monitoraggio] -->

</div>

---

## Licenza

Questo progetto è distribuito sotto licenza personale. Consulta il file LICENSE per i dettagli.
