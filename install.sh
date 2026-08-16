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

download_asset() {
    local version="$1" asset="$2" output="$3"
    url="https://github.com/$REPO/releases/download/$version/$asset"
    echo "  Download: $asset"
    curl -fsL -o "$output" "$url" || {
        echo "  Fallito download di $asset"
        return 1
    }
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
VERSION=""
for arg in "$@"; do
    case "$arg" in
        --local) FORCE_LOCAL=true ;;
        *) VERSION="$arg" ;;
    esac
done

# ──────────────────────────────────────────────
# 2. Determina versione
# ──────────────────────────────────────────────
BUILD_LOCAL=true
if $FORCE_LOCAL; then
    echo "  Build locale forzata (--local)"
else
    if [ -z "$VERSION" ]; then
        echo "Recupero ultima release da GitHub..."
        VERSION=$(get_latest_release) || true
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
    for old in AniDownloader.service AniDownloader.timer AniDownloaderWeb.service; do
        systemctl --user stop "$old" 2>/dev/null || true
        systemctl --user disable "$old" 2>/dev/null || true
    done
    # Rimuovi file vecchi
    rm -f "$SYSTEMD_DIR/AniDownloader.service" \
          "$SYSTEMD_DIR/AniDownloader.timer" \
          "$SYSTEMD_DIR/AniDownloaderWeb.service"
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
    cmake -B "$BUILD_DIR" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR"

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
            FLATPAK_TMP=$(mktemp -d)
            # Copia pulita dal commit corrente: flatpak/ è già committato nel
            # repo, quindi git archive include manifest, cache npm/cargo e
            # modulo aria2 (niente build/ o build-repo/ sporchi dal workspace).
            git archive HEAD | tar -x -C "$FLATPAK_TMP"
            pushd "$FLATPAK_TMP" >/dev/null
            if flatpak-builder --user --force-clean --jobs="$(nproc)" --ccache \
                --repo=flatpak/build-repo flatpak/build \
                flatpak/com.anidownloader.desktop.yml >"$RPT/flatpak-build.log" 2>&1; then
                flatpak build-bundle flatpak/build-repo \
                    "$INSTALL_DIR/$APP_NAME.flatpak" com.anidownloader.desktop
                popd >/dev/null
                echo "  Installo Flatpak (user)..."
                flatpak --user install -y --reinstall "$INSTALL_DIR/$APP_NAME.flatpak"
                echo "  ✅ Desktop Flatpak installato: flatpak run com.anidownloader.desktop"
            else
                popd >/dev/null
                echo "  ❌ Build flatpak fallita (vedi $RPT/flatpak-build.log)"
                INSTALL_DESKTOP=false
            fi
            rm -rf "$FLATPAK_TMP"
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
            tar -xzf "$HEADLESS_TMP/headless.tar.gz" -C "$HEADLESS_DIR"
            HEADLESS_BIN=$(find "$HEADLESS_DIR" -name "anidownloaderd" -type f 2>/dev/null | head -1)
            if [ -n "$HEADLESS_BIN" ]; then
                chmod +x "$HEADLESS_BIN"
                ln -sf "$HEADLESS_BIN" "$INSTALL_DIR/anidownloaderd"
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

    # Crea anche un timer per download automatici (silent mode)
    rm -f "$SYSTEMD_DIR/anidownloader-check.service" "$SYSTEMD_DIR/anidownloader-check.timer"

    cat << EOF > "$SYSTEMD_DIR/anidownloader-check.service"
[Unit]
Description=AniDownloader check nuovi episodi
After=network-online.target

[Service]
Type=oneshot
ExecStart=$HEADLESS_BIN
StandardOutput=journal
StandardError=journal
EOF

    cat << EOF > "$SYSTEMD_DIR/anidownloader-check.timer"
[Unit]
Description=AniDownloader check periodico
Requires=anidownloader-check.service

[Timer]
OnBootSec=5min
OnUnitActiveSec=15min
OnClockChange=true
OnTimezoneChange=true
Persistent=true

[Install]
WantedBy=default.target
EOF

    systemctl --user daemon-reload
    systemctl --user enable --now anidownloaderd.service
    systemctl --user enable --now anidownloader-check.timer

    echo "  anidownloaderd.service: attivo (web UI su http://localhost:8989)"
    echo "  anidownloader-check.timer: attivo (check ogni 15 min in modalitá silenziosa)"
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
