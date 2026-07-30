# Bug Analysis — AniDownloader Planning & Download

## Bug A: `lastDownloadedEpisode` salvato per singolo episodio

**Problema**: Il callback `onTaskFinished` (sia in `main.cpp` che `WebServer.cpp`) salva `lastDownloadedEpisode` su disco OGNI VOLTA che un singolo episodio termina. Poiché i worker dell'execution engine girano in parallelo, più task della STESSA serie possono completare concorrentemente, causando:

1. **Race condition**: Thread A carica JSON (last=0), Thread B carica JSON (last=0), A salva (last=4), B salva (last=1) → il lavoro di A viene perso
2. **`lastDownloadedEpisode` potenzialmente scorretto**: Se Ep4 finisce prima di Ep2, viene salvato 4 anche se Ep2 non è ancora stato processato
3. **I/O ridondante**: Ogni task carica e salva l'intero JSON (~16KB) in modo indipendente

**File affetti**:
| File | Linee | Ruolo |
|------|-------|-------|
| `src/core/ExecutionEngine.cpp` | 131-169 | Callback `onTaskFinished` eseguito per ogni task |
| `src/core/ExecutionEngine.hpp` | 12-20 | `TaskReport` passato al callback |
| `src/main.cpp` | 214-237 | CLI — salvataggio `lastDownloadedEpisode` nel callback |
| `src/web/WebServer.cpp` | 750-768 | Web — salvataggio `lastDownloadedEpisode` nel callback |
| `src/core/SeriesRepository.cpp` | 41-57 | `saveSeriesData` sovrascrive l'intero file JSON |

**Fix proposto**: Aggregare i risultati in `ExecutionEngine` con una mappa thread-safe (`seriesName → maxEpisodeNumber`). Dopo che TUTTI i worker hanno finito (alla fine di `run()`), salvare `lastDownloadedEpisode` una volta sola per serie. Rimuovere il salvataggio dal callback `onTaskFinished`.

---

## Bug B: Sniffing batch produce URL/episodio sbagliato

**Sintomo osservato** (run del 30/07):
- 3 candidati online sniffati in 1 batch, ritornati 2 URL
- Download riuscito (`DL 16s + Conv 109s`) ha prodotto `Dogulwang_Ep_02_SUB_ITA.mp4` con **contenuto dell'Episodio 4**
- Download fallito: "Errore nel download per aria2" dopo 3 tentativi

**Interpretazione**: Lo sniffing (`sniffBatch` in `AnimeWScraper`) ha associato l'URL video dell'Ep4 a un tab con `episodeNumber=2`, invece che a Ep4. Il task risultante aveva `{episodeNumber=2, videoUrl=ep4_url}`.

**File affetti**:
| File | Linee | Ruolo |
|------|-------|-------|
| `src/scrapers/AnimeWScraper.cpp` | 91-231 | `sniffBatch` — sniffing multitab concorrente |
| `src/scrapers/AnimeWScraper.cpp` | 212-220 | Assegnazione `videoUrl` → `DownloadTask` |
| `src/scrapers/AnimeWScraper.cpp` | 256-313 | `getCandidates` — generazione candidati da HTML statico |

**Possibili cause** (da verificare con log runtime):
1. Il log performance di ChromeDriver non è correttamente isolato per tab nonostante `switchToWindow` in alcune versioni di ChromeDriver
2. Race condition nel loop di sniffing tra tab dello stesso batch quando alcuni timeout scattano simultaneamente
3. `getCandidates` restituisce candidati con `episodeNumber` non allineato all'URL della pagina

**Per diagnosticare**: Aggiungere log in `sniffBatch` che stampi per ogni tab, al termine:
```
[episodeNumber=X] pageUrl=Y → videoUrl=Z
```
e in `getCandidates`:
```
candidate[local=N] pageUrl=Y  (da data-episode-num e href)
```

---

## Bug C: Pianificazione non rileva file danneggiati/sballati (correlato)

**Problema**: `computeNextNeeded` in `ScraperUtils.cpp:266-283` cerca all'indietro da `lastDownloadedEpisode` e restituisce il primo numero valido+1. Se Ep2 ha contenuto di Ep4 (file valido all'ffprobe), la logica lo considera "a posto" e non lo riscarica mai.

**File**:
| File | Linee |
|------|-------|
| `src/scrapers/ScraperUtils.cpp` | 266-283: `computeNextNeeded` |
| `src/scrapers/ScraperUtils.cpp` | 251-264: `validEpisodePath` (solo ffprobe, nessun check contenuto) |

**Nota**: Non facile da fixare senza un modo per verificare il contenuto effettivo (es. durata video, hash). Per ora documentato.

---

## Riepilogo priorità

| Bug | Priorità | Fixabile subito? |
|-----|----------|-----------------|
| **A** — `lastDownloadedEpisode` race condition | Alta | **Sì** |
| **B** — Sniffing URL sbagliato | Alta | **No** (servono log) |
| **C** — File danneggiati non rilevati | Media | Parzialmente (dipende da B) |
