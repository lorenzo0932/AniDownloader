# Distribuzione

## Installer Linux — `install.sh`

Script bash interattivo che supporta tre modalità.

### Modalità

1. **Solo Desktop** — AppImage + launcher `.desktop` + icona
2. **Solo Headless** — binary CLI + servizio systemd + timer
3. **Entrambi** (default)

### Flusso

```
install.sh
  │
  ├── 0. Check dipendenze (curl, aria2c, ffmpeg)
  │
  ├── 1. Menu interattivo
  │
  ├── 2. Determina versione
  │   ├── GitHub API → latest release tag
  │   └── fallback: build locale
  │
  ├── 3. Crea directory (~/.local/bin, ~/.config/AniDownloader, ...)
  │
  ├── 4. Ferma processi in esecuzione
  │   └── pkill + systemctl stop
  │
  ├── 5. Installazione
  │   ├── Build locale (cmake + npm)
  │   │   ├── frontend Svelte → embed
  │   │   ├── cmake build
  │   │   └── [Desktop] Tauri build AppImage
  │   └── Oppure download da GitHub Releases
  │       ├── AppImage precompilato
  │       └── tar.gz headless
  │
  ├── 6. Icona (resources/logo.png → ~/.local/share/icons/)
  │
  ├── 7. Desktop entries (3 shortcut)
  │   ├── AniDownloader.desktop         (GUI nativa)
  │   ├── AniDownloader-CLI.desktop     (terminale + burst)
  │   └── AniDownloader-Web.desktop     (browser + helper)
  │
  ├── 8. Servizio systemd
  │   ├── anidownloaderd.service        (web server persistente)
  │   └── anidownloader-check.{service,timer} (check ogni 15 min)
  │
  └── 9. Riepilogo
```

### Desktop entries

| File | Exec | Terminal |
|------|------|----------|
| `AniDownloader.desktop` | `AniDownloader.AppImage` | No |
| `AniDownloader-CLI.desktop` | `AniDownloader --burst` | Sì |
| `AniDownloader-Web.desktop` | Script helper → browser | No |

### Systemd

**`anidownloaderd.service`** — server web persistente:
```
ExecStart=anidownloaderd --web --silent
Restart=on-failure
```

**`anidownloader-check.service`** — controllo one-shot:
```
ExecStart=anidownloaderd
Type=oneshot
```

**`anidownloader-check.timer`** — esecuzione periodica:
```
OnBootSec=5min
OnUnitActiveSec=15min
Persistent=true
```

## Installer Windows — `install.ps1`

Equivalente PowerShell dell'installer Linux. Installa in `%LOCALAPPDATA%`
e crea shortcut nel menu Start.

## GitHub Releases

Trigger: push di tag `v*` → GitHub Actions produce e carica:

| Artefatto | Piattaforma | Formato |
|-----------|-------------|---------|
| Desktop AppImage | Linux | `.AppImage` |
| Headless daemon | Linux | `.tar.gz` (binary + web dist) |
| Desktop | Windows | `.exe` (NSIS) |
| Desktop | macOS | `.dmg` |

### Naming convention

```
AniDownloader-v2.0.0-x86_64.AppImage
anidownloaderd-v2.0.0-linux-x86_64.tar.gz
AniDownloader_2.0.0_x64-setup.exe
AniDownloader_2.0.0_x64.dmg
```

## GitHub Actions

`.github/workflows/release.yml`:
- Trigger: `tags: v*`
- Matrix: `ubuntu-latest`, `windows-latest`, `macos-latest`
- Steps: checkout → build C++ → build frontend → [Tauri bundle] → upload

## Installazione headless manuale

```bash
# Download da GitHub Releases
curl -fsSL "https://github.com/lorenzo0932/AniDownloader/releases/download/v2.0.0/anidownloaderd-v2.0.0-linux-x86_64.tar.gz" \
  | tar xzf - -C ~/.local/share/anidownloader-headless/

# Oppure via install.sh
./install.sh   # scegli opzione 2
```

## Path installazione

| Componente | Linux | Windows |
|------------|-------|---------|
| Binary desktop | `~/.local/bin/AniDownloader.AppImage` | `%LOCALAPPDATA%\AniDownloader\AniDownloader.exe` |
| Binary headless | `~/.local/share/anidownloader-headless/anidownloaderd` | — |
| Config | `~/.config/AniDownloader/` | `%APPDATA%\AniDownloader\` |
| Log | `~/.config/AniDownloader/logs/` | `%APPDATA%\AniDownloader\logs\` |
| Desktop entry | `~/.local/share/applications/` | Start Menu |
| Systemd | `~/.config/systemd/user/` | — |
| Icon | `~/.local/share/icons/` | — |
