# Distribuzione Flatpak

AniDownloader può essere distribuito come **Flatpak** — la soluzione raccomandata
per il problema del white-screen AppImage (WebKitGTK bundle della distro di build
che crasha su distro diverse).

## Perché Flatpak

- Il runtime `org.gnome.Platform` **include WebKitGTK** già compilato e testato,
  identico su tutte le distro → **niente bundling** di librerie di sistema →
  niente mixing di versioni glib/WebKit → niente crash su Fedora/Ubuntu/etc.
- Sandbox isolato: l'app gira con permessi espliciti (wayland/x11, rete, home).
- Aggiornabile via Flathub o file `.flatpak` / `.flatpakref`.

## Prerequisiti

```bash
# Fedora
sudo dnf install flatpak flatpak-builder
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo

# Runtime + SDK + estensioni (branch 50)
flatpak install flathub org.gnome.Platform//50 org.gnome.Sdk//50
flatpak install flathub org.freedesktop.Sdk.Extension.node20//25.08
flatpak install flathub org.freedesktop.Sdk.Extension.rust-stable//25.08
```

## File sorgenti (rigenerabili)

| File | Generato da | Comando |
|------|-------------|---------|
| `cargo-sources.json` | `src-tauri/Cargo.lock` | `python3 flatpak-builder-tools/cargo/flatpak-cargo-generator.py -o flatpak/cargo-sources.json src-tauri/Cargo.lock` |
| `node-sources.json` | `web/package-lock.json` | `python3 -m flatpak_node_generator --no-requests-cache -o flatpak/node-sources.json npm web/package-lock.json` |
| `root-node-sources.json` | `package-lock.json` (root) | `python3 -m flatpak_node_generator --no-requests-cache -o flatpak/root-node-sources.json npm package-lock.json` |

> I generatori vengono da https://github.com/flatpak/flatpak-builder-tools.
> Rigenerarli ad **ogni cambio** di `Cargo.lock`, `web/package-lock.json` o
> `package-lock.json` root (es. bump dipendenze).
>
> `root-node-sources.json` serve per installare `@tauri-apps/cli` (dal
> `package.json` root) offline dentro il sandbox: è necessario perché il
> binario Tauri embedda il frontend solo con `tauri build` (niente `cargo build` puro).

## Build locale

Il manifest usa `type: dir` con `path: ..`: builda dal **checkout corrente**
(CI: commit della run; locale: workspace). Jobs dinamici (`--jobs="$(nproc)"`,
`CARGO_BUILD_JOBS=$(nproc)`) — si adattano alle risorse della macchina.

```bash
# build dal checkout corrente, parallelismo dinamico
flatpak-builder --user --force-clean --jobs="$(nproc)" --ccache \
  --repo=build-repo build flatpak/com.anidownloader.desktop.yml

# Test (senza installare)
flatpak-builder --user --run build flatpak/com.anidownloader.desktop.yml anidownloader

# Bundle in un file installabile
flatpak build-bundle build-repo AniDownloader.flatpak com.anidownloader.desktop
```

> NB: in locale conviene buildare da una **copia pulita** (`git archive`)
> quando il workspace contiene `src-tauri/target/` o `node_modules/` multi-GB
> (il source `type: dir` copia l'intero checkout nel sandbox). In CI il
> checkout di `actions/checkout` è già pulito.

## Installazione e lancio

```bash
flatpak --user install ./AniDownloader.flatpak
flatpak run com.anidownloader.desktop
```

## Test end-to-end del core (offline)

`fixture_flatpak.py` è una fixture HTTP locale che replica il flusso AnimeW
(pagina serie con `data-episode-num`, endpoint `/api/episode/info`, video di
prova ≥1MB servito localmente). Serve a verificare che il **core C++ dentro il
sandbox** scarichi davvero via aria2c.

```bash
# Terminale 1: avvia la fixture (porta 8899, dir per il video)
python3 flatpak/fixture_flatpak.py 8899 /tmp/ani-fixture

# Terminale 2: lancia l'app nel sandbox con la fixture come rete di test
flatpak run com.anidownloader.desktop
# oppure senza installare:
flatpak-builder --user --run build flatpak/com.anidownloader.desktop.yml anidownloader
```

Nota: il download nel sandbox è reale (aria2c incluso dal modulo `aria2.json`);
il video finto supera solo il check dimensionale (`isMediaFileHealthy` ≥1MB) —
per testare la conversione H.265 servirebbe un file video reale.

## Note sul sidecar C++ (:8989)

Il binario Tauri lancia `anidownloaderd --web` come sidecar che ascolta su
`127.0.0.1:8989` dentro il sandbox. Il `finish-args` include `--share=network`
per gli scraper e GitHub API. La webview Tauri parla al sidecar via localhost
(sempre dentro il sandbox) → funziona senza configurazione aggiuntiva.

Se serve esporre :8989 verso l'host (es. browser esterno), aggiungere al
`finish-args`: `--socket=network` non basta; serve `--share=network` (già
presente) e l'uso di `--device=all` non è necessario. Per accesso dall'esterno
del sandbox: `flatpak override --user --socket=network com.anidownloader.desktop`.

## Dipendenze runtime nel sandbox

- **`aria2`** (modulo `aria2.json`): il runtime GNOME **non** include `aria2c`,
  ma il core C++ lo usa per i download. Compilato da sorgente (solo HTTP/HTTPS:
  `--disable-bittorrent/--disable-metalink`, niente c-ares/libssh2; TLS via
  OpenSSL già presente nel runtime → nessuna lib extra). Aggiornare URL+sha256
  del tarball a ogni bump.
- **`ffmpeg`/`ffprobe`**: inclusi nel runtime GNOME (conversione + check di
  integrità) — nessun bundling necessario.
- Verificare la presenza con: `flatpak run --command=aria2c com.anidownloader.desktop --version`

## Condivisione config/cache con l'installazione nativa

Il C++ risolve i path con `XDG_CONFIG_HOME`/`XDG_CACHE_HOME` (`PathHelper`).
Flatpak li reindirizza di default in `~/.var/app/com.anidownloader.desktop/`.
I `finish-args` includono:

```
--filesystem=xdg-config/AniDownloader:create
--filesystem=xdg-cache/AniDownloader:create
```

Con questi flag, `~/.config/AniDownloader` (config.json, series_data.json) e
`~/.cache/AniDownloader` (log) del sandbox puntano alle **stesse cartelle
reali dell'host** → flatpak e installazione classica (headless/systemd)
condividono serie, configurazione e log. Verificato bidirezionalmente
(host ↔ sandbox).

## Accesso a tutti i filesystem (`--filesystem=host`)

Il manifest usa **`--filesystem=host`** (non `--filesystem=home`): i download
ed il browse del sidecar C++ possono puntare a **qualsiasi disco montato**
(`/mnt`, `/media`, `/run/media`, pool ZFS/BTRFS, ecc.), come l'installazione
nativa.

**Trade-off (deliberato)**: `--filesystem=host` rende il sandbox filesystem di
fatto inefficace. È accettabile perché l'app ha già `--share=network` e lancia
`aria2c`/`ffmpeg` come subprocess (modello "app trusted" sulla propria
macchina). NON è accettabile per una review Flathub: se un giorno si vorrà
pubblicare, andrà migrato a `xdg-desktop-portal` + permessi per-cartella
(lavoro separato).

## Canale Linux unico (AppImage deprecata)

Su Linux il **Flatpak è il canale desktop ufficiale**; l'AppImage è deprecata
(sostituita perché WebKitGTK bundle della distro di build crashava su altre
distro — white-screen).

- **CI**: `.github/workflows/flatpak.yml` builda dal commit corrente su push di
  tag `v*` e allega `AniDownloader-<ref>-linux-x86_64.flatpak` alla release.
- **Installer**: `install.sh` installa il Flatpak (download dalla release, o
  build locale con `--local`). Headless (`anidownloaderd` tar.gz) resta per
  server/NAS senza desktop environment.
- Verifica rapida del permesso host senza rebuild:
  `flatpak --user override --filesystem=host com.anidownloader.desktop`

## Pubblicazione su Flathub

Vedi https://tauri.app/distribute/flatpak/ per i requisiti di review
(metainfo completo, screenshot, icone, license). Il metainfo è già incluso
in questo manifest. **Nota**: per Flathub il permesso `--filesystem=host`
dovrebbe essere rimosso a favore dei portali (vedi sopra).
