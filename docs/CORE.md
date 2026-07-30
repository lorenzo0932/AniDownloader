# Core Engine

Namespace `Core::` — include `Core::`.

## ExecutionEngine

`include/core/ExecutionEngine.hpp` — `src/core/ExecutionEngine.cpp`

Motore principale di esecuzione. Coordina planning, scheduling e download.

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
ffmpeg -i input -c:v libx265 -crf 23 -preset veryfast -threads N output
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
