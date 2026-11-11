# Web Server e Frontend

## WebServer

Namespace `Web::` — `include/web/WebServer.hpp` — `src/web/WebServer.cpp`

Server HTTP basato su [cpp-httplib](https://github.com/yhirose/cpp-httplib).

### Ciclo di vita

```cpp
// main.cpp (modalità --web)
WebServer server(configManager, port);
server.start();                    // httplib::Server in ascolto
// ...
server.stop();
```

### Architettura

```
Thread principale:
  └── WebServer::start()
       └── setupRoutes()
       └── Scheduler::start()   (tick 30s + worker sulla coda)
       └── m_svr.listen("0.0.0.0", port)

Thread separato (per download manuale):
  └── POST /api/download/start
       └── runDownloads(seriesList, burst)
            └── ExecutionEngine::run()  (con callback SSE)

SSE:
  └── GET /api/download/events
       └── registerSseClient()
       └── loop: send events from m_sseQueues[id]
```

### Routing

Tutti gli endpoint sono definiti in `setupRoutes()`.

### SSE broadcast

Il WebServer mantiene una mappa di code SSE:
```cpp
std::mutex m_sseMutex;
std::unordered_map<uint64_t, std::queue<std::string>> m_sseQueues;
```

Ogni client SSE ha un ID univoco. Quando un download produce un evento,
viene inserito in tutte le code e i thread SSE lo spediscono al client.

### File embedding

Il frontend compilato (`web/dist/`) viene embedded nel binario C++ tramite
`scripts/embed_web.py`. Il WebServer serve questi file dalla lookup table
generata:

```cpp
auto& files = getEmbeddedFiles();
auto it = files.find(path);
if (it != files.end()) {
    res.set_content((const char*)it->second.data, it->second.size, it->second.mime_type.data());
}
```

### Cross-platform networking

`getLanIp()` rileva l'indirizzo LAN:
- Linux/macOS: `getifaddrs()`
- Windows: `GetAdaptersInfo()`

---

## Frontend Svelte 5

`web/src/` — SPA Svelte 5 con Vite.

### Componenti

| Componente | Route | Descrizione |
|------------|-------|-------------|
| `App.svelte` | — | Shell con sidebar, routing lato client |
| `StatusPage.svelte` | `/` | Home: `OverallHeader` + `RunCard` (run rilevante, task live), righe recenti, event log manuale |
| `DashboardPage.svelte` | `/gestione` | CRUD serie con griglia card / tabella densa, poster |
| `DiscoverPage.svelte` | `/scopri` | Ricerca/filtri AnimeWorld, griglia, tooltip, Aggiungi |
| `AttivitaPage.svelte` | `/attivita` | Viste Avvii/Episodi, tab Recenti/Problemi/Storico, filtro origine, espansione run/job |
| `ConfigPage.svelte` | `/config` | Configurazione app + sezione Avanzate (coda dei controlli, sola lettura) |
| `LogsPage.svelte` | `/logs` | Log viewer |

Le vecchie route `/coda` e `/pipeline` reindirizzano a `/attivita`
(`history.replaceState`); le relative voci non sono più in sidebar.

### Componenti condivisi

| Componente | Ruolo |
|------------|-------|
| `AddSeriesForm.svelte` | Form aggiunta/modifica serie (usato da Gestione e Scopri) |
| `DetailModal.svelte` | Dettaglio serie, modifica ed eliminazione; variante read-only |
| `NotificationBell.svelte` | Campanella in sidebar: badge non lette, pannello, SSE `notification`, notifica OS se la config lo prevede |
| `StoragePanel.svelte` | In Impostazioni: spazio disco/cache/parziali, pulizia guidata |
| `QueuePage.svelte` | In Impostazioni → Avanzate: coda dei controlli (`tasks`) in sola lettura, con Aggiorna |
| `SeriesCollection.svelte` | Griglia con FLIP e densità (griglia/grande) |
| `SeriesCard.svelte` | Card poster + titolo + badge |
| `SeriesList.svelte` | Lista righe per la vista tabella |
| `SeriesRow.svelte` | Riga densa (thumb, nome, episodi, azioni) |
| `SeriesTable.svelte` | Tabella gestionale con header ordinabile |
| `RecentSeriesRow.svelte` | Riga delle sezioni “Ultime” in Home |
| `ProgressList.svelte` / `TaskItem.svelte` | Avanzamento download per episodio |
| `DownloadEventLog.svelte` | Cronologia eventi del run manuale |
| `OverallHeader.svelte` | Stato globale del run, SSE, Avvia/Ferma, coda e problemi |
| `RunCard.svelte` | Card dell'avvio rilevante in Home: barra complessiva, task live con percentuali, CTA Attività |
| `RunRow.svelte` | Riga di un avvio in Attività: origine, conteggi, barra, espansione nei job, "Riprova tutti" |
| `JobRow.svelte` | Riga episodio in Attività: locandina, `StateChip`, `JobStepper`, dettagli, "Riprova" |
| `JobStepper.svelte` | Stepper delle fasi del job (in coda → download → attesa → conversione → completato) |
| `StateChip.svelte` | Etichetta e colore unici dello stato episodio (con orario di retry) |
| `DirectoryBrowser.svelte` | File picker: ricerca, dischi, preferiti, creazione, rimozione |
| `ConfirmModal.svelte` | Modale conferma |
| `Dropdown.svelte` | Dropdown riutilizzabile |

### Librerie

| File | Ruolo |
|------|-------|
| `api.js` | Client API REST + SSE `EventSource`, URL poster e `srcset` |
| `theme.svelte.js` | Tema dark/light/system con localStorage |
| `motion.js` / `motion.css` | Token e primitive di animazione (vedi `docs/UI_MOTION.md`) |

### Viste Gestione Serie

La pagina `/gestione` ha tre viste dal toggle in alto (stile Sonarr),
default **grande**: **griglia** (browsing: card 150px con poster, titolo e
badge Ep/servizio), **grande** (stesse card a 220px, poster ~330px) e
**tabella** (gestione densa: righe con miniatura 40px, nome, Episodi,
data ultimo download e badge Alta Priorità + icone Modifica/Elimina).
Vista e ordinamento (default: data inserimento, più recenti prima)
persistono in `localStorage` (`anidl.series.view` / `anidl.series.sort`,
per-browser come Sonarr). Path, URL e azioni vivono nel modale dettaglio,
con azioni anche in overlay hover su desktop.
Su mobile la griglia non scende mai sotto 2 colonne: una card portrait 2:3
a tutta larghezza riempirebbe lo schermo da sola. Sotto ~560px anche la
vista grande resta a 2 colonne (2×220px non ci starebbero: l'auto-fill
crollerebbe a 1 colonna); su tablet la grande differenzia (3 colonne).
Sempre sotto ~560px il bottone Vista grande è nascosto (coinciderebbe
con la griglia: precedente Gmail/Jellyfin) e la tabella nasconde la data
ultimo download — riga a una sola riga con servizio, Ep e badge.

### Pagina Scopri

`/scopri` interroga il catalogo AnimeWorld una pagina del sito (~40 card)
alla volta: ricerca per keyword oppure filtri (generi, stagione, anno, tipo,
stato, studio, lingua, doppiati, ordinamento). Le card usano gli stessi
componenti di Gestione; il dettaglio è un tooltip lazy (trama, voto, stato,
generi). Il badge “In gestione” e il divieto di doppia aggiunta si basano
sull'`identity` opaca restituita dal server, confrontata come stringa.
“Aggiungi” apre il form precompilato con titolo e URL.

### Modali Gestione Serie

Info e modifica condividono: overlay `fadeIn` + pannello `panelIn` (rise
+16px, visibile anche sul fullscreen mobile) in ingresso via CSS,
uscita via `out:` simmetrica (le `transition:` integrali non risultavano
percepibili all'apertura). Scroll di sfondo bloccato.
Su mobile il back `← Serie` è l'unica via (niente X); su desktop restano
X in info e Annulla/Salva nel form. Su mobile la locandina ha sfondo ambient
(stessa immagine sfocata dietro, zero traffico extra): niente bande grigie.

### Pagina Attività

`/attivita` unifica le ex pagine Coda e Pipeline in linguaggio utente.

- **Viste**: "Avvii" (default) elenca i run con origine, ora, conteggi e barra;
  "Episodi" elenca i job per episodio. La prima legge `GET /api/runs`, la
  seconda `GET /api/pipeline`.
- **Tab**: "Recenti" (in corso o conclusi nelle ultime 24h), "Problemi"
  (errori/interruzioni, anche oltre 24h; sui run mostra il contatore) e
  "Storico" (conclusi da più di 24h).
- **Filtro origine** (solo vista Avvii): Tutte / Manuale / Automatico.
- **Espansione**: un avvio carica i suoi job da `GET /api/runs/:id` e offre
  "Riprova tutti" sui falliti (`POST /api/runs/:id/retry-failed`); un episodio
  mostra dettagli (origine, tentativi, errore) e "Riprova"
  (`POST /api/pipeline/retry`). I tentativi non vengono azzerati.
- **Deep link**: `?run=<id>` apre l'avvio, `?vista=episodi` la vista episodi.
- **Polling** ogni 5 s (sospeso a pagina nascosta); i run aperti espansi vengono
  ricaricati a ogni giro.
- **Accessibilità**: tablist/tab con `aria-selected`, filtri con
  `aria-pressed`, focus visibile, messaggi di errore in `role="alert"`.

La coda dei controlli (`tasks`) non è più una pagina: vive in Impostazioni →
**Avanzate** (`QueuePage` con `readonly` e `embedded`), sola lettura e
"Aggiorna".

### Home: avvio rilevante e attività live

- **OverallHeader**: stato globale, connessione SSE, Avvia/Ferma, coda totale
  e problemi aperti, con link ad Attività.
- **RunCard**: legge `GET /api/overview`, che sceglie il run più utile (in
  corso; altrimenti il più recente con episodi; altrimenti il più recente).
  Mostra origine, stato, durata, barra complessiva (completati + frazione live
  dei task attivi sul totale) e i task live con serie, episodio, fase e
  percentuale.
- **Aggiornamento**: `overview` al mount, polling ogni 3 s quando la pagina è
  visibile e refresh con debounce (600 ms) sugli eventi SSE `progress`/
  `finished`/`done`/`queue`. I task live arrivano anche dai check automatici,
  non solo dal run manuale.
- **Sotto la card**: righe Ultime aggiunte/scaricate e `DownloadEventLog` del
  run manuale.

### Home: righe recenti cliccabili

Le righe di Ultime aggiunte/scaricate (`RecentSeriesRow`) aprono lo stesso
`DetailModal` in sola lettura (`actions=false`): niente Modifica/Elimina,
gestire resta compito di Gestione. Stesso comportamento delle righe
tabella (tutta la riga = bottone). La colonna aggiunte usa il sort server
`added` desc (più recenti prima).
Il form ricorda l'origine (modello drill-down): aperto dal dettaglio,
back e Salva riportano al dettaglio (`← [nome]` su mobile); da lista/FAB
tutto come prima. Lo switch griglia↔grande ricrea la griglia (`{#key}`,
`cardIn` sempre) + `animate:flip` per i riordini. La locandina nel dettaglio
si ingrandisce in lightbox fullscreen vincolata al viewport (max
100vw/100dvh, full-res 1080); Esc chiude prima la lightbox, poi il modale.
Animazioni standard in tutta la UI: ingressi `fly`/`fadeIn`/`panelIn`/
`scaleIn`/`slideUp`/`cardIn`/`rowIn` (0.2–0.35s ease-out), hover 0.15s.

### api.js

```js
export const api = {
    series: { list, add, update, remove, fetchName },
    config: { get, set },
    download: { start, stop, status },
    logs: (lines) => ...,
    status: () => ...,
    browse: { list, mounts, places, mkdir, touch, remove },
    cache: { thumbs, clearThumbs },
    discover: { page, tooltip },
    notifications: { list, read },
    queue: { list, retry },
    pipeline: { list, retry },
    runs: { overview, list, get, retryFailed },
    storage: { info, cleanup },
    sse: () => new EventSource('/api/download/events'),
};
```

### DirectoryBrowser

Il file picker è un componente singolo, usato sia dalla pagina Serie sia dalle
Impostazioni. Funziona identico in browser (`--web`) e nel sidecar Tauri,
perché ogni operazione passa dagli endpoint HTTP del backend C++ (nessun
accesso al filesystem dal JavaScript).

Layout a due colonne, stile file manager nativo:

- **Sidebar** (fissa a sinistra su desktop, drawer a tutta altezza su mobile
  dietro il pulsante in alto, con chiusura propria): **Posizioni**
  (`/api/browse/places`: Home, Documenti, Scaricati…), **Preferiti** (stelle),
  **Dischi** (`/api/browse/mounts`, solo volumi utili: niente snap, boot o
  pseudo-fs). Il pulsante "Aggiorna" rilegge i mount, utile per volumi
  inseriti a caldo mentre il picker è aperto.
- **Area principale**: solo il contenuto della cartella corrente (niente più
  muro di sezioni inline).
- **Dimensioni fisse** (680×560 su desktop): la finestra non si ridimensiona
  mai col contenuto; i testi lunghi si troncano con ellipsis. Su mobile va
  fullscreen in flex (la lista riempie lo spazio), con touch target ~40px,
  safe-area al posto di padding fissi e ritmo verticale compresso in
  orizzontale.

- **Ricerca**: filtro locale sui nomi della cartella corrente, senza roundtrip.
  È per-cartella: cambiando cartella si azzera da sola, così non filtra (e
  svuota) anche le cartelle successive.
- **Preferiti**: la stella nel path bar pinna il path corrente in
  `config.pinned_paths` (max 20, più recenti in cima, con rollback se il
  salvataggio fallisce). I pin su dischi non più
  montati restano visibili e riportano l'errore al clic.
- **Creazione**: `+ Cartella` / `+ File` aprono una riga inline. La validazione
  del nome è duplicata lato client per evitare un roundtrip inutile.
- **Rimozione**: l'icona cestino su ogni riga chiede conferma. Se il server
  risponde `409` (cartella non vuota) parte una **seconda** conferma con il
  conteggio degli elementi e,in quel caso, la rimozione è ricorsiva.
- **"Su"**: usa il `parent` restituito dal server invece di splittare il path,
  così funziona anche su Windows (separatore `\`).

### Tema

Supporta tre modalità:
- `dark` — tema scuro (default)
- `light` — tema chiaro
- `system` — segue preferenza sistema operativo

Persistito in `localStorage`. Le variabili CSS custom cambiano dinamicamente
tramite `data-theme` attribute su `<html>`.

### API polling

La shell `App.svelte` esegue polling ogni 5 secondi di `GET /api/download/status`
per gestire il `beforeunload` warning se ci sono download attivi.
Il progresso live viene da SSE, non dal polling.

### Build

```bash
cd web && npm install && npm run build
# Output: web/dist/
```

La build produce file statici (HTML, JS, CSS) che vengono poi embedded
nel binario C++ da `scripts/embed_web.py`.
