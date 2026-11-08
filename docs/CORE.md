# Core Engine

Namespace `Core::` — include `Core::`.

## ExecutionEngine

`include/core/ExecutionEngine.hpp` — `src/core/ExecutionEngine.cpp`

Motore principale del run manuale (CLI o `POST /api/download/start`).
Coordina planning, scheduling e download. Non è usato dallo scheduler
interno, che passa da `CheckRunner` + `WorkerPool`.

### Metodo principale

```cpp
void run(const std::vector<Series>& seriesList,
         bool burstMode,
         std::atomic<bool>& stopSignal,
         ProgressCb onProgress,
         StatusCb onStatus,
         FinishedCb onTaskFinished,
         SkippedCb onTaskSkipped,
         AnalysisCb onAnalysisDone);
```

### Flusso

1. **Planning parallelo** — per ogni serie lancia un thread separato
   che chiama `PlanningService::planSingleSeries()`. Ogni thread:
   - Istanzia lo scraper appropriato
   - Scraping della pagina episodi
   - Calcolo del prossimo episodio da scaricare
   - Restituisce `DownloadTask[]`

2. **Scheduling** — ordina per priorità (high priority prima), poi
   crea un thread pool con `maxConcurrentTasks × 2` workers.

3. **Esecuzione** — ogni worker prende il prossimo task disponibile
   (indice atomico) e chiama `MediaProcessor::processTask()`.

### Callback

| Callback | Quando | Parametri |
|----------|--------|-----------|
| `onProgress` | Per ogni update di progress | `name`, `episode`, `message` (es. `"One Piece"`, `1`, `"DL 45%"`) |
| `onStatus` | Stato globale | `overallStatus` |
| `onTaskFinished` | Task completato | `TaskReport` |
| `onTaskSkipped` | Serie già aggiornata | `name`, `reason` |
| `onAnalysisDone` | Fine fase planning | — |

### TaskReport

```cpp
struct TaskReport {
    std::string name;
    bool success;
    int episodeNumber;
    double dlTime;    // secondi
    double convTime;  // secondi
    std::string error;
};
```

---

## MediaProcessor

`include/core/MediaProcessor.hpp` — `src/core/MediaProcessor.cpp`

Gestisce download + conversione di un singolo episodio.

### processTask()

```
processTask(task, series, strategy) → ProcessResult
```

Fasi:

1. **Pre-check** — se il file esiste:
   - Se c'è `.aria2` residue → rimuove (crash recovery)
   - Se il file è corrotto (ffprobe) → rimuove
   - Se il file è già H.265 → skip (completato)
   - Se il file esiste ma non HEVC → solo conversione
2. **Download** — `aria2c -x 16 -s 16` con retry automatico (3 tentativi)
3. **Conversione** — solo se `convertToH265 == true`

### ConvertAndVerify()

Conversione FFmpeg con strategie:

**Direct encoding** (chunks ≤ 1):
```
ffmpeg -i input -c:v libx265 -crf 25 -preset veryfast -threads N output
```

**Chunked encoding** (chunks > 1, default dinamico):
```
1. Split: ffmpeg -c copy -f segment -segment_time T s%03d.mp4
2. Encode parallelo: ogni chunk con ffmpeg indipendente
3. Merge: ffmpeg -f concat -c copy merged.mp4
4. Verifica: ffprobe durata vs. originale
```

Il work directory usa `/dev/shm` (RAM disk) se RAM disponibile > 55%.

### Semaforo conversioni

Le conversioni FFmpeg sono limitate da un semaforo basato su
`condition_variable` per evitare di saturare la CPU.

```cpp
static std::mutex s_convMutex;
static std::condition_variable s_convCv;
static std::atomic<int> s_activeConversions;
```

---

## MediaProbe

`include/core/MediaProbe.hpp` — `src/core/MediaProbe.cpp`

Wrapper attorno a `ffprobe`.

| Funzione | Output |
|----------|--------|
| `getVideoDuration()` | durata in secondi |
| `getVideoCodec()` | codec name (`hevc`, `h264`, ...) |
| `isMediaFileHealthy()` | true se il file è leggibile |
| `verifyIntegrity()` | true se durata matcha expected ±5% |

Usa cache interna per evitare chiamate ffprobe ripetute.

---

## PlanningService

`include/core/PlanningService.hpp` — `src/core/PlanningService.cpp`

Fabbrica degli scraper e orchestratore del piano di download.

```cpp
static std::unique_ptr<BaseScraper> getScraperInstance(const std::string& serviceName);
static std::vector<DownloadTask> planSingleSeries(const Series& series, ...);
```

---

## Logger

`include/core/Logger.hpp` — `src/core/Logger.cpp`

Logger thread-safe con rotazione.

```cpp
Logger::init("/path/to/log");
Logger::info("message");
Logger::warn("warning");
Logger::error("error");
Logger::result("Serie", 12.5, 34.2);  // resoconto download
```

Formato output:
```
[2026-07-28 15:30:00] INFO:  Loading series data...
[2026-07-28 15:30:01] RESULT: One Piece | DL: 12.5s | Conv: 34.2s
```

---

## ProcessUtils

`include/core/ProcessUtils.hpp` — `src/core/ProcessUtils.cpp`

Esecuzione comandi shell e utilità di sistema.

```cpp
runCommand(cmd, stopSignal, onLineRead)  → exit code
getRamUsagePercent()                      → 0.0-100.0
parseProgressUs(progressFile)             → usci letto
formatFloat(value, precision)             → stringa formattata
```

---

## FileUtils

`include/core/FileUtils.hpp` — `src/core/FileUtils.cpp`

Operazioni su filesystem per serie.

```cpp
countVideoFiles(dirPath)        → conteggio
readNfoDescription(seriesPath)  → testo descrizione
findPosterPath(seriesPath)      → path poster
listDirectories(dirPath)        → vector<DirEntry>
```

---

## SeriesUtils

`include/core/SeriesUtils.hpp` — `src/core/SeriesUtils.cpp`

```cpp
sortSeries(json& data, field, desc);
```

---

## LogUtils

`include/core/LogUtils.hpp` — `src/core/LogUtils.cpp`

```cpp
getRecentLines(logPath, n)  → ultime N righe
```

---

## UpdateChecker

`include/core/UpdateChecker.hpp` — `src/core/UpdateChecker.cpp`

Check aggiornamenti via GitHub API.

```cpp
checkForUpdates("2.0.0", callback)  → UpdateInfo
compareVersions("2.0.0", "2.1.0")   → -1, 0, +1
```

Usa `https://api.github.com/repos/lorenzo0932/AniDownloader/releases/latest`
con `cpr` per HTTP GET. Confronto semver rigoroso.

---

## Scheduler e coda persistente (ADR-004)

### Scheduler

`include/core/Scheduler.hpp` — `src/core/Scheduler.cpp`

Sostituisce i timer OS. Un thread di tick valuta ogni 30 s se il controllo
periodico è maturo e accoda un task `check`; l'esecuzione è del `WorkerPool`.
Intervallo da config `scheduling.intervalloMinuti` (default 15, range 5–1440).

```cpp
Scheduler scheduler(config, jsonPath);
scheduler.start();
scheduler.pausa(true);   // i task restano in coda
scheduler.stato();       // { attivo, inPausa, checkInCoda, ultimoGiro }
```

Stop cooperativo: niente nuovi giri, attesa di quello in corso, mai kill.

### WorkerPool

`include/core/WorkerPool.hpp` — `src/core/WorkerPool.cpp`

N thread che reclamano task dalla coda ed eseguono l'handler registrato per
`kind`. Retry con backoff progressivo 5'/15'/1h, poi `fallito` definitivo.
Eventi per SSE: `avviato`, `completato`, `riprova`, `fallito`.

### QueueStore

`include/core/QueueStore.hpp` — `src/core/QueueStore.cpp`

Coda persistente nella tabella `tasks`: `claim()` atomico, `complete()`,
`failRetry()` con backoff, `failDead()`, `resetOrfani()` per il crash
recovery. Ogni istanza apre la propria connessione (WAL + busy-timeout).

### CheckRunner

`include/core/CheckRunner.hpp` — `src/core/CheckRunner.cpp`

Un giro completo check → download → commit stato. Usato sia dalla CLI sia
dall'handler `check` dello scheduler: un solo punto di verità. Con repository
condiviso evita due istanze con mutex separati sullo stesso file. Dopo il
commit dello stato emette `onEpisodeReady(serie, episodio)` per ogni report
riuscito con episodio > 0: è l'unico punto in cui nasce una notifica (la CLI
passa un callback vuoto: è interattiva).

### NotificationStore

`include/core/NotificationStore.hpp` — `src/core/NotificationStore.cpp`

Notifiche "pronto da guardare" nella tabella SQLite `notifications` (schema
v3). Operazioni: `add`, `list(limit)` (più recenti prima), `unreadCount`,
`markRead(id)`, `markAllRead`. Ogni chiamata apre la propria connessione,
come `QueueStore`. Lo Scheduler persiste la riga e poi inoltra al WebServer
per il broadcast SSE.

---

## Database e persistenza

### Database

`include/core/Database.hpp` — `src/core/Database.cpp`

Wrapper RAII su SQLite vendored. Statement preparati con binding 1-based,
transazioni, migrazioni. Schema: `series`, `tasks`, `kv`, `migrations`.

### DbImporter

`include/core/DbImporter.hpp` — `src/core/DbImporter.cpp`

Import JSON → SQLite con backup, dry-run, transazione e verifica
conteggi + hash; rollback automatico su errore. Usato al primo avvio del DB.

### SeriesRepository

`include/core/SeriesRepository.hpp` — `src/core/SeriesRepository.cpp`

Unico punto di mutazione delle serie. Il DB è la fonte; il JSON è export e
fallback di rollback. Cache invalidata su (mtime,size) del DB.
Mutazioni atomiche `addSeries`/`updateSeries`/`removeSeries` con vincolo
anti-duplicato (identity = servizio + URL canonico).

---

## Altri moduli

### ThumbCache

`include/core/ThumbCache.hpp` — `src/core/ThumbCache.cpp`

Miniature WebP su disco per `GET /api/poster`: tier 32/96/480/720/1080 px,
chiave FNV-1a su path+mtime+size+w+q, tetto 500 MB con eviction LRU.

### InstanceLock

`include/core/InstanceLock.hpp` — `src/core/InstanceLock.cpp`

Lock esclusivo non bloccante su file (flock su POSIX, handle esclusivo su
Windows). Protegge un giro completo: snapshot → download → commit.
