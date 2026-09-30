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
| `DashboardPage.svelte` | `/gestione` | CRUD serie con griglia card, poster |
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

### api.js

```js
export const api = {
    series: { list, add, update, remove, fetchName },
    config: { get, set },
    download: { start, stop, status },
    logs: (lines) => ...,
    status: () => ...,
    browse: { list, mounts, mkdir, touch, remove },
    sse: () => new EventSource('/api/download/events'),
};
```

### DirectoryBrowser

Il file picker è un componente singolo, usato sia dalla pagina Serie sia dalle
Impostazioni. Funziona identico in browser (`--web`) e nel sidecar Tauri,
perché ogni operazione passa dagli endpoint HTTP del backend C++ (nessun
accesso al filesystem dal JavaScript).

- **Ricerca**: filtro locale sui nomi della cartella corrente, senza roundtrip.
- **Dischi**: sezione con i volumi montati (`/api/browse/mounts`), per non
  dover risalire a mano a `/`.
- **Preferiti**: la stella nel path bar pinna il path corrente in
  `config.pinned_paths` (max 20, più recenti in cima). I pin su dischi non più
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
