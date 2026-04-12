# 🚀 AniDownloader C++ (Qt6 Engine Edition)

Una riscrittura completa, ad alte prestazioni, del sistema **AniDownloader**. Questo porting in **C++17** con **Qt6** è progettato per utenti che necessitano di massima velocità, efficienza e un'integrazione nativa profonda con i sistemi Linux, con l'obiettivo di una futura espansione cross-platform.

<div align="center">
  <!-- [PLACEHOLDER: Immagine principale della nuova interfaccia Qt6] -->
  <p><i>L'interfaccia moderna e scattante basata su Qt6</i></p>
</div>

---

## ✨ Perché il Porting in C++?

Il passaggio dal core Python al C++ non è stato solo per "sfida", ma per abbattere i limiti del Global Interpreter Lock (GIL) e gestire flussi di lavoro massivi su hardware di fascia alta:

* **Multithreading Nativo**: Gestione granulare dei thread tramite `QtConcurrent` ed `ExecutionEngine`. Il software satura intelligentemente la CPU senza bloccare l'I/O.
* **Interfaccia Qt6 Moderna**: Una GUI nativa, leggera e reattiva che sostituisce completamente la vecchia dipendenza da Python.
* **Zero Latency**: Lo scraping e il planning delle serie sono istantanei grazie alle librerie `CPR` e `nlohmann_json`.
* **Gestione RAM Intelligente**: Monitoraggio costante dell'uso della memoria e ottimizzazione dei processi FFmpeg.
* **Integrazione Systemd & Tray**: Un servizio in background per monitoraggi automatici e una comoda icona nel vassoio di sistema per il controllo rapido.

---

## 🛠 Requisiti di Sistema

* **OS**: Linux (Fedora, Ubuntu, Arch, etc.)
* **Dipendenze Core**: `aria2c`, `ffmpeg` (devono essere presenti nel PATH).
* **Librerie**: `Qt6` (Widgets, Concurrent), `libcurl`, `openssl`.
* **Build Tools**: `cmake` (>= 3.17), `gcc/g++` (supporto C++17).

---

## 📥 Installazione e Deploy (Linux)

Il sistema include uno script di installazione "One-Click" (`install.sh`) che configura l'intero ecosistema utente:

```bash
chmod +x install.sh
./install.sh
```

**Lo script esegue automaticamente:**

1. Compilazione del binario ottimizzato (Release mode).
2. Installazione dell'eseguibile in `~/.local/bin/`.
3. Configurazione degli asset e delle icone.
4. Registrazione del file `.desktop` nel menu applicazioni.
5. Attivazione del **Timer Systemd** per i controlli automatici.

---

## 🏗 Struttura del Progetto

```text
AniDownloader_dev/
├── include/                # Header files (.hpp)
│   ├── core/               # Logica centrale: ExecutionEngine, MediaProcessor, Series
│   ├── gui/                # Interfaccia Qt6: MainWindow, Dialogs, Custom Widgets
│   ├── config/             # Gestione configurazione e percorsi
│   └── scrapers/           # Definizioni degli scraper (AnimeW, AnimeU)
├── src/                    # Implementazioni (.cpp)
│   ├── core/               # Logica di processing e gestione dati
│   ├── gui/                # Implementazione UI e logica dei segnali/slot
│   ├── config/             # Implementazione configurazione
│   ├── scrapers/           # Parser nativi per i vari servizi
│   └── main.cpp            # Entry point dell'applicazione
├── external/               # Dipendenze esterne gestite tramite FetchContent
├── systemd_services/       # Automazione Linux (AniDownloader.service/timer)
├── resources/              # Asset grafici e icone
├── legacy_python/          # Riferimenti storici del codice originale
├── install.sh              # Script per installazione e deploy rapido
└── CMakeLists.txt          # Sistema di build principale
```

---

## 🚀 Roadmap e Funzionalità Future (TODO)

### 🖥 Compatibilità e Porting

* [ ] **Porting Completo a Windows**: Adattare il core per funzionare su Windows, gestendo i percorsi Windows-style e sostituendo systemd con il Task Scheduler. (In corso)
* [x] **GUI C++ Nativa**: Sviluppata in Qt6 per massima velocità e integrazione.
* [x] **Notifiche Desktop Native**: Integrazione con il sistema di notifiche Linux.

### 📡 Scrapers & Download

* [ ] **Porting AnimeU Scraper**: Completare il porting per la gestione di tutti i provider video.
* [x] **Gestione Priorità**: Possibilità di definire la priorità di download per ogni serie.

### ⚙️ Engine Core

* [x] **Tray Icon**: Controllo dell'applicazione e stato dei download dal vassoio di sistema.
* [x] **Caching Immagini**: Sistema di cache per i poster delle serie per una UI più fluida.

---

## 📊 Monitoraggio e Log

Oltre alla GUI, puoi monitorare l'attività del servizio systemd in tempo reale:

```bash
journalctl --user -u AniDownloader.service -f
```

<!-- [PLACEHOLDER: Screenshot della gestione serie o dei log] -->
