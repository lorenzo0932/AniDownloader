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
       └── m_svr.listen("0.0.0.0", port)

Thread separato (per download):
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
| `StatusPage.svelte` | `/` | Dashboard download live + stato serie |
| `DashboardPage.svelte` | `/gestione` | CRUD serie con griglia card / tabella densa, poster |
| `ConfigPage.svelte` | `/config` | Configurazione app |
| `LogsPage.svelte` | `/logs` | Log viewer |

### Librerie

| File | Ruolo |
|------|-------|
| `api.js` | Client API REST + SSE `EventSource` |
| `theme.svelte.js` | Tema dark/light/system con localStorage |
| `Dropdown.svelte` | Componente dropdown riutilizzabile |
| `ConfirmModal.svelte` | Modale conferma |
| `DirectoryBrowser.svelte` | File picker: ricerca, dischi, preferiti, creazione, rimozione |
| `SeriesCard.svelte` | Card essenziale (poster + titolo + badge), azioni in hover overlay |
| `SeriesTable.svelte` | Vista tabella densa per la gestione (thumb + nome + azioni icona) |

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

### Modali Gestione Serie

Info e modifica condividono: overlay `fade`, pannello `fly` (+16px rise,
visibile anche sul fullscreen mobile), scroll di sfondo bloccato.
Su mobile il back `← Serie` è l'unica via (niente X); su desktop restano
X in info e Annulla/Salva nel form.
Il form ricorda l'origine (modello drill-down): aperto dal dettaglio,
back e Salva riportano al dettaglio (`← [nome]` su mobile); da lista/FAB
tutto come prima. Lo switch griglia↔grande usa `animate:flip`: le card
scivolano nelle nuove posizioni. La locandina nel dettaglio si ingrandisce
in lightbox fullscreen vincolata al viewport (max 100vw/100dvh,
full-res 1080); Esc chiude prima la lightbox, poi il modale.
Animazioni standard in tutta la UI: ingressi `fly`/`fadeIn`/`scaleIn`/
`slideUp`/`cardIn`/`rowIn` (0.2–0.35s ease-out), hover 0.15s. Modali,
lightbox e pagine usano `transition:` simmetriche: anche la chiusura
è animata, con gli stessi tempi dell'apertura.

### api.js

```js
export const api = {
    series: { list, add, update, remove, fetchName },
    config: { get, set },
    download: { start, stop, status },
    logs: (lines) => ...,
    status: () => ...,
    browse: { list, mounts, places, mkdir, touch, remove },
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

La `StatusPage` esegue polling ogni 5 secondi di `GET /api/download/status`
per gestire il `beforeunload` warning se ci sono download attivi.
Il progresso live viene da SSE, non dal polling.

### Build

```bash
cd web && npm install && npm run build
# Output: web/dist/
```

La build produce file statici (HTML, JS, CSS) che vengono poi embedded
nel binario C++ da `scripts/embed_web.py`.
