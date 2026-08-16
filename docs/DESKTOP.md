# Desktop — Tauri v2 Wrapper

`src-tauri/` — wrapper nativo Rust [Tauri v2](https://v2.tauri.app/).

## Architettura

```
┌─────────────────────────────────────┐
│         Tauri Shell (Rust)           │
│  ┌─────────────┐  ┌──────────────┐  │
│  │ Tray Icon   │  │ WebView      │  │
│  │ Menu:       │  │ (Svelte 5)   │  │
│  │  Mostra     │  │              │  │
│  │  Nascondi   │  │ Porta 8989   │  │
│  │  Esci       │  │ (sidecar)    │  │
│  └─────────────┘  └──────┬───────┘  │
│                          │           │
│  ┌───────────────────────▼────────┐  │
│  │ Sidecar: anidownloaderd --web  │  │
│  │ (C++ binary, background)       │  │
│  └────────────────────────────────┘  │
└─────────────────────────────────────┘
```

## Componenti

### `src-tauri/src/main.rs`

Il binary Rust:
1. **Setup tray** — icona nel tray di sistema con menu contestuale
   - "Mostra" → mostra finestra
   - "Nascondi" → nascondi finestra
   - "Esci" → kill sidecar + exit
2. **Avvia sidecar** — lancia `anidownloaderd --web` come processo figlio
3. **Crea finestra** — `WebviewWindow` che carica `index.html` (frontend Svelte)
4. **Cleanup** — kill sidecar su `ExitRequested`

### `src-tauri/tauri.conf.json`

```json
{
    "productName": "AniDownloader",
    "version": "2.1.0",
    "bundle": {
        "externalBin": ["binaries/anidownloaderd"],
        "targets": ["appimage", "nsis", "dmg"]
    }
}
```

## Sidecar

Il binary C++ viene copiato in `src-tauri/binaries/` da CMake
post-build con il naming convention Tauri:
`anidownloaderd-{target-triple}[.exe]`

Tauri lo estrae e lo lancia automaticamente con `--web`.

## Port conflict detection

Se il sidecar è già in ascolto sulla 8989, Tauri non lo rilancia:

```rust
let port_in_use = TcpStream::connect_timeout(
    &"127.0.0.1:8989".parse().unwrap(),
    Duration::from_millis(200),
).is_ok();
```

## Build

```bash
npm run tauri:build   # tauri-cli pinnato in package.json (riproducibile)
```

Output:
- Linux: `src-tauri/target/release/bundle/appimage/AniDownloader_*.AppImage`
- macOS: `src-tauri/target/release/bundle/dmg/AniDownloader_*.dmg`
- Windows: `src-tauri/target/release/bundle/nsis/AniDownloader_*_x64-setup.exe`

### Plugin Tauri

```toml
[dependencies]
tauri = { version = "=2.11.5", features = ["tray-icon"] }
tauri-plugin-notification = "=2.3.3"
tauri-plugin-dialog = "=2.7.2"
tauri-plugin-fs = "=2.5.1"
tauri-plugin-shell = "=2.3.5"
```

> Le versioni sono pinnate (`=`) per riproducibilità tra CI e build locale
> (vedi fix white-screen 2.1.0). Per aggiornarle: bump su `Cargo.toml`
> e rigenerare `Cargo.lock`.
