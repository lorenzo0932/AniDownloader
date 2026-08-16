# Distribuzione

## Installer Linux — `install.sh`

Script bash interattivo che supporta tre modalità.

### Modalità

1. **Solo Desktop** — installa il **Flatpak** `com.anidownloader.desktop`
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
  │   ├── default → GitHub API → ultima release STABILE (tag semver)
  │   ├── --dev   → GitHub API → ultima PRERELEASE (tag vX.Y.Z-dev.N),
  │   │             fallback a stabile se non ci sono prerelease
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
  │   │   └── [Desktop] flatpak-builder (build dal checkout corrente)
  │   └── Oppure download da GitHub Releases
  │       ├── Flatpak desktop (.flatpak) → flatpak --user install
  │       └── tar.gz headless
  │
  ├── 6. Icona (resources/logo.png → ~/.local/share/icons/)
  │
  ├── 7. Desktop entries
  │   ├── (GUI nativa: gestita dal Flatpak stesso)
  │   ├── AniDownloader-CLI.desktop     (terminale + burst, solo headless)
  │   └── AniDownloader-Web.desktop     (browser + helper, solo headless)
  │
  ├── 8. Servizio systemd (solo headless)
  │   ├── anidownloaderd.service        (web server persistente)
  │   └── anidownloader-check.{service,timer} (check ogni 15 min)
  │
  └── 9. Riepilogo
```

### Desktop entries (headless)

| File | Exec | Terminal |
|------|------|----------|
| `AniDownloader-CLI.desktop` | `anidownloaderd --burst` | Sì |
| `AniDownloader-Web.desktop` | Script helper → browser | No |

> La GUI nativa su Linux è distribuita come **Flatpak** (canale Linux unico,
> AppImage deprecata): il manifest installa già la desktop-entry + icona in
> `/app`, quindi `install.sh` non crea più la entry GUI manuale.

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

| Artefatto | Piattaforma | Formato | Workflow |
|-----------|-------------|---------|----------|
| Desktop **Flatpak** | Linux | `.flatpak` | `flatpak.yml` |
| Headless daemon | Linux | `.tar.gz` (binary + web dist) | `release.yml` |
| Desktop | Windows | `.exe` (NSIS) | `release.yml` |
| Desktop | macOS | `.dmg` | `release.yml` |

> Su Linux il **Flatpak è il canale desktop ufficiale** (AppImage deprecata).

### Rilasci automatici (release-please)

Il bump della versione e il tag sono gestiti da **release-please**
(`.github/workflows/release-please.yml`, config `release-please-config*.json`):

| Branch | Canale | Tag | Contenuto |
|--------|--------|-----|-----------|
| `dev`  | pre-release | `vX.Y.Z-dev.N` | Prerelease: bump dei file di versione su conventional commits, niente CHANGELOG (`skip-changelog`). Al merge del PR `release-please` crea il tag prerelease → build artefatti dev. |
| `main` | stabile     | `vX.Y.Z`      | Release stabile: raccoglie i commit dall'ultima release, aggiorna files + `CHANGELOG.md`. Al merge crea il tag → build artefatti release. |

Il merge del PR di rilascio (o di promozione `dev → main`) è sempre
un'azione esplicita del maintainer; il tag è la *conseguenza* di quel merge.
Prerequisiti GitHub (una tantum): secret `RELEASE_PLEASE_TOKEN` (PAT con
`contents: write`) usato dal workflow — gli eventi generati dal `GITHUB_TOKEN`
non triggerano altri workflow — e `Settings → Actions → Allow GitHub Actions
to create and approve pull requests`.

File di versione aggiornati automaticamente: `CMakeLists.txt`
(annotazione `x-release-please-version`), `package*.json` root + `web/`,
`src-tauri/{Cargo.toml,Cargo.lock}`, `tauri.conf.json` non più incluso
(la versione deriva da `Cargo.toml`), metainfo Flatpak
(`x-release-please-version-date`).

### Naming convention

```
AniDownloader-v2.1.0-linux-x86_64.flatpak
anidownloaderd-v2.1.0-linux-x86_64.tar.gz
AniDownloader_2.1.0_x64-setup.exe      (NSIS)
AniDownloader_2.1.0_aarch64.dmg
```

Pattern reali (vedi `.github/workflows/`):
- Flatpak: `AniDownloader-<ref_name>-linux-x86_64.flatpak` (`flatpak.yml`)
- Headless: `anidownloaderd-<ref_name>-linux-<arch>.tar.gz` (Linux),
  `anidownloaderd-<ref_name>-win-<arch>.zip` (Windows),
  `anidownloaderd-<ref_name>-mac-<arch>.tar.gz` (macOS)
- Windows: `bundle/nsis/*.exe`; macOS: `bundle/dmg/*.dmg`

## GitHub Actions

`.github/workflows/release.yml` (Windows/macOS + headless Linux):
- Trigger: `tags: v*`
- Matrix: `ubuntu-24.04`, `windows-2022`, `macos-14`
- Toolchain **pinnata**: Rust `1.96.0` (`rust-toolchain.toml`), tauri-cli
  `2.11.4` (`package.json`), Node LTS — build riproducibile tra CI e locale.
- Linux: build C++ → **CTest** → headless tar.gz (NON più AppImage).
- Windows/macOS: bundle NSIS/dmg.
- Cache: `actions/cache` per `~/.cargo` e `node_modules` sui 3 job

`.github/workflows/flatpak.yml` (desktop Linux, workflow dedicato):
- Trigger: `tags: v*` + `workflow_dispatch`
- Builda il **commit corrente** del checkout (manifest `type: dir`) con
  `flatpak/flatpak-github-actions` (container `gnome-50`), poi allega
  `AniDownloader-<ref>-linux-x86_64.flatpak` alla release.
- Cache: `.flatpak-builder` gestita dall'azione.

La verifica CI è riproducibile anche in locale via Docker:

```bash
# build C++ + CTest + launch test webview in un container ubuntu:24.04
bash ci/verify.sh   # vedi ci/Dockerfile
```

## Installazione headless manuale

```bash
# Download da GitHub Releases
curl -fsSL "https://github.com/lorenzo0932/AniDownloader/releases/download/v2.1.0/anidownloaderd-v2.1.0-linux-x86_64.tar.gz" \
  | tar xzf - -C ~/.local/share/anidownloader-headless/

# Oppure via install.sh
./install.sh   # scegli opzione 2
```

## Path installazione

| Componente | Linux | Windows |
|------------|-------|---------|
| Desktop | Flatpak `com.anidownloader.desktop` (`flatpak run`) | `%LOCALAPPDATA%\AniDownloader\AniDownloader.exe` |
| Bundle desktop | `~/.local/bin/AniDownloader.flatpak` | — |
| Binary headless | `~/.local/share/anidownloader-headless/anidownloaderd` | — |
| Config | `~/.config/AniDownloader/` | `%APPDATA%\AniDownloader\` |
| Log | `~/.config/AniDownloader/logs/` | `%APPDATA%\AniDownloader\logs\` |
| Desktop entry (headless) | `~/.local/share/applications/` | Start Menu |
| Systemd | `~/.config/systemd/user/` | — |
| Icon | `~/.local/share/icons/` | — |
