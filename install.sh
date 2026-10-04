#!/bin/bash
set -euo pipefail

APP_NAME="AniDownloader"
REPO="lorenzo0932/AniDownloader"
INSTALL_DIR="$HOME/.local/bin"
APP_DIR="$HOME/.local/share/applications"
SYSTEMD_DIR="$HOME/.config/systemd/user"
ICON_DEST_DIR="$HOME/.local/share/icons"
ICON_NAME="anidownloader_logo.png"
ICON_FULL_PATH="$ICON_DEST_DIR/$ICON_NAME"
CONFIG_DIR="$HOME/.config/$APP_NAME"
HEADLESS_DIR="$HOME/.local/share/anidownloader-headless"

# ──────────────────────────────────────────────
# Funzioni
# ──────────────────────────────────────────────
get_latest_release() {
    curl -sL "https://api.github.com/repos/$REPO/releases/latest" | \
        grep '"tag_name"' | cut -d'"' -f4
}

# Ultima PRERELEASE semver (canale dev, tag `vX.Y.Z-dev.N` prodotti da
# release-please). I tag legacy come `v2.1.2_dev` (underscore) non matchano
# e vengono ignorati.
get_latest_dev() {
    curl -sL "https://api.github.com/repos/$REPO/releases?per_page=100" | \
        grep -oP '"tag_name": "\Kv?[0-9]+\.[0-9]+\.[0-9]+-[0-9A-Za-z.-]+' | \
        sort -V | tail -1
}

download_asset() {
    local version="$1" asset="$2" output="$3"
    url="https://github.com/$REPO/releases/download/$version/$asset"
    echo "  Download: $asset"
    curl -fsL -o "$output" "$url" || {
        echo "  Fallito download di $asset"
        return 1
    }
}

# Job di compilazione dai core FISICI (non i thread di nproc): con SMT nproc
# conta il doppio e 32 job di rustc/link LTO esplodono la RAM. Clamp 2..32.
detect_jobs() {
    local phys=""
    if [ -r /proc/cpuinfo ]; then
        phys=$(awk '/^physical id/{p=$4} /^core id/{c=$4; print p":"c}' /proc/cpuinfo 2>/dev/null | sort -u | wc -l | tr -d ' ')
    fi
    case "$phys" in
        ''|*[!0-9]*|0) phys="" ;;
    esac
    if [ -z "$phys" ]; then
        if command -v sysctl &>/dev/null && sysctl -n hw.physicalcpu &>/dev/null; then
            phys=$(sysctl -n hw.physicalcpu 2>/dev/null || true)
        else
            phys=$(nproc 2>/dev/null || echo 4)
        fi
    fi
    case "$phys" in
        ''|*[!0-9]*) phys=4 ;;
    esac
    if [ "$phys" -lt 2 ]; then phys=2; fi
    if [ "$phys" -gt 32 ]; then phys=32; fi
    printf '%s' "$phys"
}

# ──────────────────────────────────────────────
# 0. Verifica dipendenze
# ──────────────────────────────────────────────
MISSING=""

check_cmd() {
    if ! command -v "$1" &>/dev/null; then
        MISSING="$MISSING  - $1 ($2)\n"
    fi
}

check_cmd curl "download pre-built artifacts"
check_cmd aria2c "runtime (download multi-thread) — opzionale ma raccomandato"
check_cmd ffmpeg "runtime (conversione video) — opzionale ma raccomandato"

if [ -n "$MISSING" ]; then
    echo "═══════════════════════════════════════════════"
    echo " Dipendenze mancanti:"
    echo -e "$MISSING"
    echo "═══════════════════════════════════════════════"
fi

# ──────────────────────────────────────────────
# 1. Menu
# ──────────────────────────────────────────────
clear 2>/dev/null || true
cat << "EOF"
╔══════════════════════════════════════════════════════════════╗
║                  AniDownloader — Installer                   ║
╚══════════════════════════════════════════════════════════════╝

EOF

echo "Cosa vuoi installare?"
echo ""
echo "  1) Solo Desktop (Flatpak)  [~50 MB]"
echo "     App nativa con icona nel drawer, tray icon."
echo "     Include Tauri + C++ backend + Web UI integrata."
echo "     Distribuzione ufficiale Linux (AppImage deprecata)."
echo ""
echo "  2) Solo Headless (CLI + servizio systemd)  [~8 MB]"
echo "     Solo backend C++ per server/NAS/Raspberry Pi."
echo "     Accesso via browser sulla porta 8989."
echo "     Il daemon 'anidownloaderd' parte automaticamente."
echo ""
echo "  3) Entrambi (consigliato)  [~58 MB]"
echo "     Desktop (Flatpak) + Headless. App nativa + servizio systemd."
echo ""
echo "  0) Annulla"
echo ""

read -r -p "Scelta [0-3] (default: 3): " choice
choice="${choice:-3}"

INSTALL_DESKTOP=false
INSTALL_HEADLESS=false

case "$choice" in
    0) echo "Annullato."; exit 0 ;;
    1) INSTALL_DESKTOP=true ;;
    2) INSTALL_HEADLESS=true ;;
    3) INSTALL_DESKTOP=true; INSTALL_HEADLESS=true ;;
    *) echo "Scelta non valida."; exit 1 ;;
esac

# Il desktop su Linux usa Flatpak (canale unico, AppImage deprecata).
if $INSTALL_DESKTOP; then
    if ! command -v flatpak &>/dev/null; then
        echo ""
        echo "⚠️  flatpak non trovato — serve per il Desktop (canale Linux unico)."
        echo "    Installa flatpak e riprova, oppure scegli solo Headless (2)."
        echo "    Fedora:  sudo dnf install flatpak"
        echo "    Debian:  sudo apt install flatpak"
        echo ""
        exit 1
    fi
fi

echo ""

# ──────────────────────────────────────────────
# 1b. Parsing argomenti
# ──────────────────────────────────────────────
FORCE_LOCAL=false
FORCE_DEV=false
CLEAN_BUILD=false
FLATPAK_EPHEMERAL=false
VERSION=""
for arg in "$@"; do
    case "$arg" in
        --local) FORCE_LOCAL=true ;;
        --dev) FORCE_DEV=true ;;
        --clean) CLEAN_BUILD=true ;;
        *) VERSION="$arg" ;;
    esac
done
JOBS="$(detect_jobs)"
echo "Job di compilazione: $JOBS (core fisici; --clean per build pulita)"

# ──────────────────────────────────────────────
# 2. Determina versione
# ──────────────────────────────────────────────
BUILD_LOCAL=true
if $FORCE_LOCAL; then
    echo "  Build locale forzata (--local)"
else
    if [ -z "$VERSION" ]; then
        if $FORCE_DEV; then
            echo "Recupero ultima prerelease da GitHub (--dev)..."
            VERSION=$(get_latest_dev) || true
            if [ -z "$VERSION" ]; then
                echo "  Nessuna prerelease disponibile, uso l'ultima release stabile."
                VERSION=$(get_latest_release) || true
            fi
        else
            echo "Recupero ultima release da GitHub..."
            VERSION=$(get_latest_release) || true
        fi
        if [ -n "$VERSION" ]; then
            echo "  Trovata: $VERSION"
            BUILD_LOCAL=false
        else
            echo "  GitHub non raggiungibile. Procedo con build locale."
        fi
    else
        echo "  Versione richiesta: $VERSION"
    fi
fi
echo ""

# ──────────────────────────────────────────────
# 3. Crea directory necessarie
# ──────────────────────────────────────────────
mkdir -p "$INSTALL_DIR" "$APP_DIR" "$SYSTEMD_DIR" "$ICON_DEST_DIR" "$CONFIG_DIR"
if $INSTALL_HEADLESS; then
    mkdir -p "$HEADLESS_DIR"
fi

# ──────────────────────────────────────────────
# 4. Rileva systemd
# ──────────────────────────────────────────────
HAS_SYSTEMD=false
if command -v systemctl &>/dev/null && systemctl --user show-environment &>/dev/null 2>&1; then
    HAS_SYSTEMD=true
fi

# ──────────────────────────────────────────────
# 5. Ferma istanze in esecuzione
# ──────────────────────────────────────────────
echo "Fermo eventuali processi in esecuzione..."
pkill -f "$INSTALL_DIR/$APP_NAME" 2>/dev/null || true
pkill -f "$INSTALL_DIR/AniDownloader" 2>/dev/null || true
pkill -f "$INSTALL_DIR/anidownloaderd" 2>/dev/null || true
pkill -f "$HEADLESS_DIR/anidownloaderd" 2>/dev/null || true
if $HAS_SYSTEMD; then
    # Ferma servizi nuovi
    systemctl --user stop anidownloaderd.service 2>/dev/null || true
    # Ferma e disabilita servizi VECCHI (retrocompatibilità)
    for old in AniDownloader.service AniDownloader.timer AniDownloaderWeb.service \
               anidownloader-check.service anidownloader-check.timer; do
        systemctl --user stop "$old" 2>/dev/null || true
        systemctl --user disable "$old" 2>/dev/null || true
    done
    # Rimuovi file vecchi
    rm -f "$SYSTEMD_DIR/AniDownloader.service" \
          "$SYSTEMD_DIR/AniDownloader.timer" \
          "$SYSTEMD_DIR/AniDownloaderWeb.service" \
          "$SYSTEMD_DIR/anidownloader-check.service" \
          "$SYSTEMD_DIR/anidownloader-check.timer"
    systemctl --user daemon-reload
fi
sleep 1

# ──────────────────────────────────────────────
# 5. Installazione
# ──────────────────────────────────────────────
ARCH=$(uname -m)
case "$ARCH" in
    x86_64)  ARCH="x86_64"  ;;
    aarch64) ARCH="aarch64" ;;
    armv7l)  ARCH="armv7l"  ;;
    *)       echo "  Architettura '$ARCH' non supportata. Uso x86_64 come default."
             ARCH="x86_64"  ;;
esac

if [ "$ARCH" != "x86_64" ]; then
    echo ""
    echo "⚠️  Attenzione: architettura $ARCH"
    echo "    Il Flatpak desktop è pubblicato solo per x86_64. Su $ARCH"
    echo "    funziona la build locale del daemon headless:"
    echo "    ./install.sh --local  (poi scelta 2 — Headless)"
    echo ""
fi

if $BUILD_LOCAL; then
    echo "═══ Build locale ═══"

    check_cmd cmake "build system"
    check_cmd node "frontend build (npm)"
    check_cmd npm "frontend build"

    BUILD_DIR="build"
    if command -v ninja &>/dev/null; then
        GENERATOR="Ninja"
        BUILD_CMD="ninja"
    else
        GENERATOR="Unix Makefiles"
        BUILD_CMD="make -j$(nproc)"
    fi
    echo "Build frontend web + embed into C++ binary..."
    (cd web && npm install --silent && npm run build --silent) || true
    # input_dir è RELATIVO a web_dir (embed_web.py fa os.chdir); output_hpp invece
    # deve essere ASSOLUTO, altrimenti i file generati finiscono dentro web/.
    python3 scripts/embed_web.py web dist "$PWD/include/web/embedded_web.hpp"

    mkdir -p "$BUILD_DIR"
    # ccache dimezza i rebuild (solo se installato; il primo rebuild dopo
    # l'abilitazione e' full una tantum per cambio compiler).
    CMAKE_LAUNCHER_ARGS=""
    if command -v ccache &>/dev/null; then
        CMAKE_LAUNCHER_ARGS="-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
    fi
    if $CLEAN_BUILD; then
        echo "Build pulita richiesta (--clean): rimuovo $BUILD_DIR"
        rm -rf "$BUILD_DIR"
        mkdir -p "$BUILD_DIR"
    fi
    # Espansione non quotata voluta (word splitting sugli argomenti).
    # shellcheck disable=SC2086
    cmake -B "$BUILD_DIR" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Release $CMAKE_LAUNCHER_ARGS
    cmake --build "$BUILD_DIR" -j "$JOBS"

    # Copia sempre il binary C++ raw in INSTALL_DIR per la CLI
    cp "$BUILD_DIR/$APP_NAME" "$INSTALL_DIR/AniDownloader"
    chmod +x "$INSTALL_DIR/AniDownloader"

    if $INSTALL_DESKTOP; then
        # ── Desktop via Flatpak (canale Linux unico, AppImage deprecata) ──
        # Build dal checkout CORRENTE (manifest usa type:dir path:..), con una
        # copia pulita del repo: evita di copiare nel sandbox target/ e
        # node_modules multi-GB presenti nel workspace locale (nella CI il
        # checkout di actions/checkout è già pulito, qui replichiamo lo stesso
        # comportamento con git archive).
        echo "Build Flatpak desktop..."
        check_cmd flatpak-builder "build flatpak — installa con: sudo dnf install flatpak-builder / sudo apt install flatpak-builder"
        check_cmd flatpak "distribuzione flatpak — installa con: sudo dnf install flatpak / sudo apt install flatpak"
        if [ ! -d "flatpak" ]; then
            echo "  ❌ cartella flatpak/ non trovata (serve il repo completo)"
            INSTALL_DESKTOP=false
        else
            # Workdir per la build Flatpak: PERSISTENTE (fuori repo, in cache).
            # L'incrementalità la dà la cache di stato (.flatpak-builder nel
            # CWD, mai cancellata), NON il riuso della app dir: --force-clean
            # SEMPRE (svuota solo flatpak/build e ricompone dai moduli in
            # cache con "Cache hit, skipping build"). Senza --force-clean il
            # builder si rifiuta se l'app dir esiste. --clean cancella anche
            # lo stato → rebuild totale vero. Solo senza rsync (niente sync
            # delta affidabile) si usa una dir effimera.
            FLATPAK_WORK="${XDG_CACHE_HOME:-$HOME/.cache}/anidownloader-flatpak-build"
            FLATPAK_EPHEMERAL=false
            if ! command -v rsync &>/dev/null; then
                echo "  ⚠️  rsync assente: uso build pulita effimera (lenta)"
                FLATPAK_TMP=$(mktemp -d)
                FLATPAK_EPHEMERAL=true
            else
                if $CLEAN_BUILD; then
                    echo "  Build Flatpak PULITA (--clean): cancello anche la cache di stato"
                    rm -rf "$FLATPAK_WORK"
                else
                    echo "  Build Flatpak incrementale (--clean per ripartire da zero)"
                fi
                mkdir -p "$FLATPAK_WORK"
                FLATPAK_TMP="$FLATPAK_WORK"
            fi
            FLATPAK_LOG="$FLATPAK_TMP/flatpak-build.log"
            # Copia dei sorgenti nel workdir: il manifest usa `type: dir` con
            # `path: ..` → serve la ROOT del repo (web/, src/, scripts/, ...).
            # In modo incrementale si usa rsync --delete sul worktree (include
            # anche modifiche non committate: per build locali è il
            # comportamento giusto, come la parte headless) con gli stessi
            # exclude + log di build (anche i file rimossi dal repo spariscono
            # dalla copia, niente stato stantio). Solo nel fallback effimero
            # (mktemp) si usa git archive o copia selettiva.
            if [ "$FLATPAK_EPHEMERAL" = false ]; then
                rsync -a --delete --exclude '.git' --exclude 'build' --exclude 'node_modules' \
                    --exclude 'src-tauri/target' --exclude 'src-tauri/binaries' \
                    --exclude 'web/dist' --exclude 'web/node_modules' \
                    --exclude 'flatpak/build' --exclude 'flatpak/build-repo' \
                    --exclude '.flatpak-builder/' --exclude 'flatpak-build.log' \
                    --exclude 'plan/' --exclude '.opencode/' \
                    --exclude 'AniDownloader.flatpak' --exclude '*.AppImage' \
                    . "$FLATPAK_TMP/" || { echo "  ❌ rsync verso $FLATPAK_TMP fallito"; exit 1; }
            elif git archive HEAD 2>/dev/null | tar -x -C "$FLATPAK_TMP" 2>/dev/null; then
                :
            elif command -v rsync &>/dev/null; then
                rsync -a --exclude '.git' --exclude 'build' --exclude 'node_modules' \
                    --exclude 'src-tauri/target' --exclude 'src-tauri/binaries' \
                    --exclude 'web/dist' --exclude 'web/node_modules' \
                    --exclude 'flatpak/build' --exclude 'flatpak/build-repo' \
                    --exclude 'AniDownloader.flatpak' --exclude '*.AppImage' \
                    . "$FLATPAK_TMP/"
            else
                # Ultimo fallback senza rsync: copia della root escludendo gli
                # artefatti pesanti via find/cp.
                mkdir -p "$FLATPAK_TMP"
                cp -r CMakeLists.txt CMakePresets.json README.md package.json \
                    package-lock.json rust-toolchain.toml include src web scripts \
                    resources tests flatpak docs licenses ci install.sh install.ps1 \
                    uninstall.sh uninstall.ps1 CHANGELOG.md THIRD_PARTY_NOTICES.md \
                    src-tauri "$FLATPAK_TMP/" 2>/dev/null
            fi
            # Smonta eventuali mount FUSE orfani di build interrotte (bloccano
            # rm -rf e inquinano i mount visti dal picker): best-effort, mai
            # fatale per l'installazione. Il "|| true" finale è obbligatorio:
            # con pipefail, findmnt che non trova nulla sotto il path
            # ritorna 1 e set -e ucciderebbe lo script in silenzio.
            if command -v findmnt &>/dev/null; then
                findmnt -R -o TARGET -n "$FLATPAK_TMP" 2>/dev/null | while IFS= read -r mnt; do
                    if command -v fusermount3 &>/dev/null; then
                        fusermount3 -u "$mnt" 2>/dev/null || true
                    else
                        umount "$mnt" 2>/dev/null || true
                    fi
                done || true
            fi
            pushd "$FLATPAK_TMP" >/dev/null
            # --force-clean sempre: svuota solo la app dir, i moduli restano
            # in cache di stato ("Cache hit, skipping build"). --keep-build-dirs
            # conserva gli alberi di build (costa GB, serve all'incrementale).
            if flatpak-builder --user --force-clean --keep-build-dirs --jobs="$JOBS" --ccache \
                --repo=flatpak/build-repo flatpak/build \
                flatpak/com.anidownloader.desktop.yml >"$FLATPAK_LOG" 2>&1; then
                flatpak build-bundle flatpak/build-repo \
                    "$INSTALL_DIR/$APP_NAME.flatpak" com.anidownloader.desktop
                popd >/dev/null
                echo "  Installo Flatpak (user)..."
                flatpak --user install -y --reinstall "$INSTALL_DIR/$APP_NAME.flatpak"
                echo "  ✅ Desktop Flatpak installato: flatpak run com.anidownloader.desktop"
            else
                popd >/dev/null
                echo "  ❌ Build flatpak fallita (vedi $FLATPAK_LOG)"
                INSTALL_DESKTOP=false
            fi
            if $FLATPAK_EPHEMERAL; then
                rm -rf "$FLATPAK_TMP"
            fi
        fi
    fi

    if $INSTALL_HEADLESS; then
        echo "Installo headless..."
        mkdir -p "$HEADLESS_DIR/web"
        cp "$BUILD_DIR/$APP_NAME" "$HEADLESS_DIR/anidownloaderd"
        if [ -d "web/dist" ]; then
            rm -rf "$HEADLESS_DIR/web"/*
            cp -r web/dist/* "$HEADLESS_DIR/web/"
        fi
        ln -sf "$HEADLESS_DIR/anidownloaderd" "$INSTALL_DIR/anidownloaderd"
    fi
else
    echo "═══ Download da GitHub ═══"

    if $INSTALL_DESKTOP; then
        # ── Desktop via Flatpak (canale Linux unico, AppImage deprecata) ──
        echo "Scarico Flatpak desktop..."
        DESKTOP_ASSET="${APP_NAME}-${VERSION}-linux-${ARCH}.flatpak"
        DESKTOP_DEST="$INSTALL_DIR/$APP_NAME.flatpak"
        if download_asset "$VERSION" "$DESKTOP_ASSET" "$DESKTOP_DEST"; then
            echo "  Installo Flatpak (user)..."
            flatpak --user install -y --reinstall "$DESKTOP_DEST"
            echo "  ✅ Desktop Flatpak installato: flatpak run com.anidownloader.desktop"
        else
            echo "  Flatpak non pubblicato per $ARCH. Salto il Desktop."
            echo "  Usa './install.sh --local' per la build da sorgente."
            INSTALL_DESKTOP=false
        fi
    fi

    if $INSTALL_HEADLESS; then
        echo "Scarico headless..."
        HEADLESS_ASSET="anidownloaderd-${VERSION}-linux-${ARCH}.tar.gz"
        HEADLESS_TMP=$(mktemp -d)
        if download_asset "$VERSION" "$HEADLESS_ASSET" "$HEADLESS_TMP/headless.tar.gz"; then
            # Estrai in temp e installa su path FISSO: con residui di versioni
            # precedenti, `find` su HEADLESS_DIR pescava il binario vecchio
            # (caso reale: installata la 2.2.0 al posto della 3.0.0-dev.3).
            tar -xzf "$HEADLESS_TMP/headless.tar.gz" -C "$HEADLESS_TMP"
            FRESH_BIN=$(find "$HEADLESS_TMP" -name "anidownloaderd" -type f 2>/dev/null | head -1)
            if [ -n "$FRESH_BIN" ]; then
                cp -f "$FRESH_BIN" "$HEADLESS_DIR/anidownloaderd"
                chmod +x "$HEADLESS_DIR/anidownloaderd"
                HEADLESS_BIN="$HEADLESS_DIR/anidownloaderd"
                FRESH_WEB="$(dirname "$FRESH_BIN")/web"
                if [ -d "$FRESH_WEB" ]; then
                    rm -rf "$HEADLESS_DIR/web"
                    cp -r "$FRESH_WEB" "$HEADLESS_DIR/web"
                fi
                ln -sf "$HEADLESS_BIN" "$INSTALL_DIR/anidownloaderd"
                # Pulizia residui versionati di installazioni precedenti.
                find "$HEADLESS_DIR" -maxdepth 1 -name "anidownloaderd-*" -exec rm -rf {} + 2>/dev/null || true
            else
                echo "  Binary anidownloaderd non trovato nell'archivio"
            fi
        else
            echo "  Download fallito. Salto headless."
        fi
        rm -rf "$HEADLESS_TMP"
    fi
fi

# ──────────────────────────────────────────────
# 6. Icona
# ──────────────────────────────────────────────
if [ -f "resources/logo.png" ]; then
    cp -f "resources/logo.png" "$ICON_FULL_PATH"
elif [ -f "$HEADLESS_DIR/resources/logo.png" ]; then
    cp -f "$HEADLESS_DIR/resources/logo.png" "$ICON_FULL_PATH"
fi

# ──────────────────────────────────────────────
# 7. Shortcut .desktop
# ──────────────────────────────────────────────
# NB: la GUI desktop è distribuita come Flatpak (canale Linux unico) e il
# manifest installa già la sua desktop-entry + icona in /app — qui creiamo
# SOLO le entry headless (CLI + WebUI), che il flatpak non fornisce.
# Pulisce vecchi file .desktop (incluse le entry AppImage deprecate)
rm -f "$APP_DIR/$APP_NAME.desktop" \
      "$APP_DIR/${APP_NAME}GUI.desktop" \
      "$APP_DIR/${APP_NAME}Web.desktop"

# 7a. (rimossa) GUI — gestita dal Flatpak

# 7b. CLI — terminale con dashboard burst (solo se headless installato)
if $INSTALL_HEADLESS; then
    CLI_BIN="$INSTALL_DIR/anidownloaderd"
    cat << EOF > "$APP_DIR/${APP_NAME}-CLI.desktop"
[Desktop Entry]
Version=1.0
Type=Application
Name=AniDownloader (CLI)
Comment=Download e conversione anime — dashboard ANSI live
Exec=$CLI_BIN --burst
Path=$HOME
Icon=$ICON_FULL_PATH
Terminal=true
Categories=Network;Video;AudioVideo;
EOF
fi

# 7c. WebUI — browser + auto-avvio daemon (solo headless)
if $INSTALL_HEADLESS; then
    # Crea script helper per WebUI
    WEBUI_HELPER="$INSTALL_DIR/anidownloader-webui.sh"

    cat << 'SCRIPT' > "$WEBUI_HELPER"
#!/bin/bash
# Helper per aprire WebUI: avvia il daemon se non in ascolto, poi apre browser
WEBUI_PORT=8989

# Trova il binary headless
find_binary() {
    local dirs=(
        "__HEADLESS_DIR__/anidownloaderd"
        "__INSTALL_DIR__/anidownloaderd"
    )
    for p in "${dirs[@]}"; do
        if [ -x "$p" ]; then
            echo "$p"
            return 0
        fi
    done
    return 1
}

HEADLESS_BIN=$(find_binary)

# Verifica se il daemon è già in ascolto
if command -v ss &>/dev/null; then
    LISTENING=$(ss -tlnp "sport = :$WEBUI_PORT" 2>/dev/null)
elif command -v netstat &>/dev/null; then
    LISTENING=$(netstat -tlnp 2>/dev/null | grep ":$WEBUI_PORT ")
else
    LISTENING=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$WEBUI_PORT" 2>/dev/null)
fi

if [ -z "$LISTENING" ] || [ "$LISTENING" = "000" ]; then
    echo "Avvio AniDownloader WebUI (porta $WEBUI_PORT)..."
    if [ -n "$HEADLESS_BIN" ]; then
        "$HEADLESS_BIN" --web --silent &
        sleep 2
    else
        echo "Binary non trovato in nessun path."
        echo "Installa AniDownloader prima di usare questa voce."
        exit 1
    fi
fi

# Apri browser
if command -v xdg-open &>/dev/null; then
    xdg-open "http://127.0.0.1:$WEBUI_PORT"
elif command -v sensible-browser &>/dev/null; then
    sensible-browser "http://127.0.0.1:$WEBUI_PORT"
else
    echo "Apri il browser su http://127.0.0.1:$WEBUI_PORT"
fi
SCRIPT

    # Sostituisce i placeholder coi path reali
    sed -i "s|__HEADLESS_DIR__|$HEADLESS_DIR|g; s|__INSTALL_DIR__|$INSTALL_DIR|g" "$WEBUI_HELPER"
    chmod +x "$WEBUI_HELPER"

    cat << EOF > "$APP_DIR/${APP_NAME}-Web.desktop"
[Desktop Entry]
Version=1.0
Type=Application
Name=AniDownloader (WebUI)
Comment=Download e conversione anime — interfaccia web
Exec=$WEBUI_HELPER
Path=$HOME
Icon=$ICON_FULL_PATH
Terminal=false
Categories=Network;Video;AudioVideo;
EOF
fi

update-desktop-database "$APP_DIR" 2>/dev/null || true
echo "  Desktop entries creati: AniDownloader{,-CLI,-Web}.desktop"

# ──────────────────────────────────────────────
# 8. Servizio systemd (solo Headless)
# ──────────────────────────────────────────────
if $INSTALL_HEADLESS; then
    HEADLESS_BIN="$HEADLESS_DIR/anidownloaderd"
    [ -x "$HEADLESS_BIN" ] || HEADLESS_BIN="$INSTALL_DIR/anidownloaderd"
fi

if $INSTALL_HEADLESS && $HAS_SYSTEMD; then

    rm -f "$SYSTEMD_DIR/anidownloaderd.service"

    cat << EOF > "$SYSTEMD_DIR/anidownloaderd.service"
[Unit]
Description=AniDownloader Headless Daemon
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=$HEADLESS_BIN --web --silent
Restart=on-failure
RestartSec=10
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=default.target
EOF

    # Timer legacy (pre-3.0): lo scheduler è interno al demone (ADR-004).
    # Su upgrade: ferma, disabilita e rimuove le unità del timer.
    systemctl --user stop anidownloader-check.timer anidownloader-check.service 2>/dev/null || true
    systemctl --user disable anidownloader-check.timer anidownloader-check.service 2>/dev/null || true
    rm -f "$SYSTEMD_DIR/anidownloader-check.service" "$SYSTEMD_DIR/anidownloader-check.timer"

    systemctl --user daemon-reload
    systemctl --user enable --now anidownloaderd.service

    echo "  anidownloaderd.service: attivo (web UI su http://localhost:8989 + scheduler interno)"
fi

if $INSTALL_HEADLESS && ! $HAS_SYSTEMD; then
    echo "  systemd non disponibile. Avvia manualmente:"
    echo "    $HEADLESS_BIN --web --silent &"
    echo "  Oppure usa lo script di init della tua distribuzione."
fi

# ──────────────────────────────────────────────
# 9. Riepilogo
# ──────────────────────────────────────────────
echo ""
echo "╔═══════════════════════════════════════════════╗"
echo "║  Installazione completata!                    ║"
echo "╚═══════════════════════════════════════════════╝"
echo ""

if $INSTALL_DESKTOP; then
    echo "  Desktop (Flatpak): com.anidownloader.desktop"
    echo "  Avvio:    flatpak run com.anidownloader.desktop"
    echo "  Bundle:   $INSTALL_DIR/$APP_NAME.flatpak"
    echo "  (AppImage deprecata — il canale desktop Linux è il Flatpak)"
fi

if $INSTALL_HEADLESS; then
    echo "  Headless: $HEADLESS_DIR/anidownloaderd"
    if $HAS_SYSTEMD; then
        echo "  Servizio: anidownloaderd.service (systemd)"
    fi
    echo "  Web UI:   http://localhost:8989"
fi

echo "  Config:   $CONFIG_DIR"
echo ""
