# 🚀 AniDownloader C++ (Engine Edition)

Una riscrittura completa, ad alte prestazioni, del sistema **AniDownloader**. Questo porting in **C++17** è progettato per utenti che necessitano di massima velocità, efficienza e un'integrazione nativa profonda con i sistemi Linux, con l'obiettivo di una futura espansione cross-platform.

<div align="center">

</div>

---

## ✨ Perché il Porting in C++?

Il passaggio dal core Python al C++ non è stato solo per "sfida", ma per abbattere i limiti del Global Interpreter Lock (GIL) e gestire flussi di lavoro massivi su hardware di fascia alta (ottimizzato per 32+ thread):

* **Multithreading Nativo**: Gestione granulare dei thread per FFmpeg. Il software satura intelligentemente la CPU senza bloccare l'I/O.
* **Zero Latency**: Lo scraping e il planning delle serie sono istantanei grazie alle librerie `CPR` e `nlohmann_json`.
* **Gestione RAM Intelligente**: Utilizzo di `/dev/shm` per minimizzare l'usura dei dischi (SSD/NVMe) durante la conversione.
* **Integrazione Systemd**: Un servizio leggerissimo che monitora le tue serie in background ogni 15 minuti con impatto minimo.

---

## 🛠 Requisiti di Sistema

* **OS**: Linux (Fedora, Ubuntu, Arch, etc.)
* **Dipendenze Core**: `aria2c`, `ffmpeg` (devono essere presenti nel PATH).
* **Build Tools**: `cmake`, `ninja` (consigliato), `gcc/g++` (supporto C++17).

---

## 📥 Installazione e Deploy (Linux)

Il sistema include uno script di installazione "One-Click" (`install.sh`) che configura l'intero ecosistema utente:

```bash
chmod +x install.sh
./install.sh

```

**Lo script esegue automaticamente:**

1. Compilazione del binario ottimizzato.
2. Installazione dell'eseguibile in `~/.local/bin/`.
3. Copia dell'icona in `~/.local/share/icons/anidownloader_logo.png`.
4. Registrazione del file `.desktop` nel menu applicazioni.
5. Attivazione del **Timer Systemd** per i controlli automatici.

---

## 🏗 Struttura del Progetto

```text
AniDownloader_dev/
├── include/                # Header files (.hpp) - Interfacce
│   ├── core/               # Logica centrale: MediaProcessor, PlanningService, Series
│   ├── config/             # Gestione configurazione: AppConfigManager, PathHelper
│   └── scrapers/           # Definizioni degli scraper (AnimeW, AnimeU)
├── src/                    # Implementazioni (.cpp)
│   ├── core/               # Implementazione logica di processing e pianificazione
│   ├── config/             # Implementazione gestione percorsi e config
│   ├── scrapers/           # Implementazione parser nativi
│   └── main.cpp            # Entry point dell'applicazione
├── external/               # Dipendenze esterne gestite localmente (es. nlohmann/json)
├── systemd_services/       # Automazione Linux (AniDownloader.service/timer)
├── resources/              # Asset grafici (logo.png)
├── legacy_python/          # Riferimenti del codice originale in Python
│   ├── AniDownloaderGUI/   # Vecchia interfaccia PyQt6
│   └── anidownloader_core/ # Vecchi scraper e logica Python
├── install.sh              # Script per installazione e deploy rapido
├── CMakeLists.txt          # Sistema di build principale
└── README.md               # Questa documentazione

```

---

## 🚀 Roadmap e Funzionalità Future (TODO)

Il porting è in fase attiva. Ecco gli obiettivi prioritari:

### 🖥 Compatibilità e Porting

* [ ] **Porting Completo a Windows**: Adattare il core per funzionare su Windows, gestendo i percorsi Windows-style e sostituendo systemd con il Task Scheduler.
* [ ] **GUI C++ Leggera**: Sviluppo di un'interfaccia grafica nativa (ImGui o Qt6 C++) per eliminare la dipendenza da Python/PyQt6. **FATTO PER LINUX**

### 📡 Scrapers & Download

* [ ] **Porting AnimeU Scraper**: Completare l'analisi per gestire il rendering JavaScript/Iframe senza dipendere da Selenium.
* [ ] **Notifiche Desktop Native**: Integrazione con `libnotify` (Linux) e `Toast Notifications` (Windows). **FATTO PER LINUX**

### ⚙️ Engine Core

* [ ] **Gestione Priorità**: Marcare serie specifiche come "Alta Priorità" per scavalcare la coda. **FATTO PER LINUX**

---

## 📊 Monitoraggio (Linux)

Monitora l'attività del servizio in background in tempo reale:

```bash
journalctl --user -u AniDownloader.service -f

```
