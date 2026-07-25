#!/bin/bash
set -euo pipefail

APP_NAME="AniDownloader"
BIN_NAME="AniDownloader"
INSTALL_DIR="$HOME/.local/bin"
APP_DIR="$HOME/.local/share/applications"
SYSTEMD_DIR="$HOME/.config/systemd/user"
ICON_DEST_DIR="$HOME/.local/share/icons"
ICON_NAME="anidownloader_logo.png"
ICON_FULL_PATH="$ICON_DEST_DIR/$ICON_NAME"
CONFIG_DIR="$HOME/.config/$APP_NAME"

# ──────────────────────────────────────────────
# 0. Verifica dipendenze
# ──────────────────────────────────────────────
MISSING=""

check_cmd() {
    if ! command -v "$1" &>/dev/null; then
        MISSING="$MISSING  - $1 ($2)\n"
    fi
}

check_pkgconfig() {
    if ! pkg-config --exists "$1" 2>/dev/null; then
        MISSING="$MISSING  - $1 ($2)\n"
    fi
}

check_cmd cmake "build system"
check_cmd ninja "build tool (alternativa: make)"

if pkg-config --exists Qt6Core 2>/dev/null; then
    :
elif command -v qt6-config &>/dev/null; then
    :
elif [ -d /usr/include/qt6 ] || [ -d /usr/include/x86_64-linux-gnu/qt6 ]; then
    :
else
    if command -v dpkg &>/dev/null; then
        MISSING="$MISSING  - Qt6 (installa: sudo apt install qt6-base-dev libqt6widgets6)\n"
    elif command -v pacman &>/dev/null; then
        MISSING="$MISSING  - Qt6 (installa: sudo pacman -S qt6-base)\n"
    elif command -v dnf &>/dev/null; then
        MISSING="$MISSING  - Qt6 (installa: sudo dnf install qt6-qtbase-devel)\n"
    else
        MISSING="$MISSING  - Qt6 (package qt6-base-dev/qt6-qtbase-devel)\n"
    fi
fi

check_pkgconfig libcurl "libcurl (sviluppo): libcurl4-openssl-dev / libcurl-devel"
check_pkgconfig openssl "OpenSSL (sviluppo): libssl-dev / openssl-devel"

check_cmd aria2c "runtime (download multi-thread) — opzionale ma raccomandato"
check_cmd ffmpeg "runtime (conversione video) — opzionale ma raccomandato"

if [ -n "$MISSING" ]; then
    echo "═══════════════════════════════════════════════"
    echo " Dipendenze mancanti:"
    echo -e "$MISSING"
    echo " Installale con il package manager della tua"
    echo " distribuzione e riprova."
    echo "═══════════════════════════════════════════════"
    exit 1
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

echo "Scegli cosa installare:"
echo ""
echo "  Il binario contiene tutte le modalità integrate:"
echo "  CLI (senza flag), GUI (--gui) e Web UI (--web)."
echo "  La scelta qui sotto determina solo quali servizi"
echo "  automatici abilitare."
echo ""
echo "  1) Binario base — nessun servizio"
echo "     Solo il binario. Avvia manualmente con --gui,"
echo "     --web o --burst. Nessun servizio in background."
echo ""
echo "  2) Binario + Timer automatico"
echo "     Aggiunge il servizio systemd che controlla nuovi"
echo "     episodi ogni 15 minuti e al risveglio dal sonno."
echo "     Consigliato per download automatici in background."
echo ""
echo "  3) Binario + Web UI"
echo "     Aggiunge il server web always-on (porta 8989)."
echo "     Gestisci tutto dal browser: download, progresso live (SSE)."
echo ""
echo "  4) Tutto (Binario + Timer + Web UI)"
echo "     Timer automatico + server web. Il massimo della"
echo "     flessibilità."
echo ""
echo "  0) Annulla"
echo ""

read -r -p "Scelta [0-4] (default: 2): " choice
choice="${choice:-2}"

WITH_TIMER=false
WITH_WEB=false

case "$choice" in
    0) echo "Annullato."; exit 0 ;;
    1) ;;
    2) WITH_TIMER=true ;;
    3) WITH_WEB=true ;;
    4) WITH_TIMER=true; WITH_WEB=true ;;
    *) echo "Scelta non valida."; exit 1 ;;
esac

echo ""

# ──────────────────────────────────────────────
# 2. Ferma servizi esistenti
# ──────────────────────────────────────────────
echo "Fermo eventuali servizi in esecuzione..."
systemctl --user stop AniDownloader.timer AniDownloader.service 2>/dev/null || true
if systemctl --user is-active AniDownloaderWeb.service &>/dev/null 2>&1; then
    systemctl --user stop AniDownloaderWeb.service 2>/dev/null || true
fi

# ──────────────────────────────────────────────
# 3. Compilazione
# ──────────────────────────────────────────────
echo "Compilazione in corso..."
BUILD_DIR="build"
if command -v ninja &>/dev/null; then
    GENERATOR="Ninja"
    BUILD_CMD="ninja"
else
    GENERATOR="Unix Makefiles"
    BUILD_CMD="make -j$(nproc)"
fi
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    CURRENT_GEN=$(grep CMAKE_MAKE_PROGRAM "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | head -1)
    if echo "$CURRENT_GEN" | grep -q "ninja" && [ "$GENERATOR" = "Unix Makefiles" ]; then
        rm -rf "$BUILD_DIR"
    elif echo "$CURRENT_GEN" | grep -q "make" && [ "$GENERATOR" = "Ninja" ]; then
        rm -rf "$BUILD_DIR"
    fi
fi
mkdir -p "$BUILD_DIR"
cmake -B "$BUILD_DIR" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR"
echo ""

# ──────────────────────────────────────────────
# 4. Crea directory necessarie
# ──────────────────────────────────────────────
mkdir -p "$INSTALL_DIR" "$APP_DIR" "$SYSTEMD_DIR" "$ICON_DEST_DIR" "$CONFIG_DIR"

# ──────────────────────────────────────────────
# 5. Installa binario
# ──────────────────────────────────────────────
echo "Installo binario in $INSTALL_DIR..."
rm -f "$INSTALL_DIR/$BIN_NAME"
cp -f "$BUILD_DIR/$BIN_NAME" "$INSTALL_DIR/"
chmod +x "$INSTALL_DIR/$BIN_NAME"

# ──────────────────────────────────────────────
# 6. Build frontend (se web)
# ──────────────────────────────────────────────
if $WITH_WEB; then
    echo "Build frontend web..."
    if command -v node &>/dev/null && command -v npm &>/dev/null; then
        if (cd web && npm install --silent && npm run build --silent); then
            mkdir -p "$INSTALL_DIR/frontend"
            cp -r web/dist/* "$INSTALL_DIR/frontend/"
        else
            echo "  Frontend build fallito (proseguo comunque)"
        fi
    else
        echo "  Node.js/npm non trovato — build frontend saltata"
    fi
fi

# ──────────────────────────────────────────────
# 7. Icona
# ──────────────────────────────────────────────
if [ -f "resources/logo.png" ]; then
    cp -f "resources/logo.png" "$ICON_FULL_PATH"
else
    echo "  resources/logo.png non trovata"
fi

# ──────────────────────────────────────────────
# 8. Shortcut .desktop
# ──────────────────────────────────────────────
rm -f "$APP_DIR/AniDownloader.desktop" "$APP_DIR/AniDownloaderGUI.desktop" "$APP_DIR/AniDownloaderWeb.desktop"

cat << EOF > "$APP_DIR/AniDownloader.desktop"
[Desktop Entry]
Version=1.0
Type=Application
Name=AniDownloader (CLI)
Comment=Download e conversione anime (riga di comando)
Exec=bash -c "$INSTALL_DIR/$BIN_NAME --burst"
Path=$HOME
Icon=$ICON_FULL_PATH
Terminal=true
Categories=Network;Video;AudioVideo;
EOF

cat << EOF > "$APP_DIR/AniDownloaderGUI.desktop"
[Desktop Entry]
Version=1.0
Type=Application
Name=AniDownloader (GUI)
Comment=Download e conversione anime (interfaccia grafica)
Exec=bash -c "$INSTALL_DIR/$BIN_NAME --gui"
Path=$HOME
Icon=$ICON_FULL_PATH
Terminal=false
Categories=Network;Video;AudioVideo;
EOF

if $WITH_WEB; then
    cat << EOF > "$APP_DIR/AniDownloaderWeb.desktop"
[Desktop Entry]
Version=1.0
Type=Application
Name=AniDownloader (Web)
Comment=Server web AniDownloader
Exec=bash -c "$INSTALL_DIR/$BIN_NAME --web"
Path=$INSTALL_DIR
Icon=$ICON_FULL_PATH
Terminal=true
Categories=Network;Video;AudioVideo;
EOF
fi

update-desktop-database "$APP_DIR" 2>/dev/null || true

# ──────────────────────────────────────────────
# 9. File systemd
# ──────────────────────────────────────────────
rm -f "$SYSTEMD_DIR/AniDownloader.service" "$SYSTEMD_DIR/AniDownloader.timer" "$SYSTEMD_DIR/AniDownloaderWeb.service"

if [ -d "systemd_services" ]; then
    sed "s|^ExecStart=.*|ExecStart=$INSTALL_DIR/$BIN_NAME|" \
        "systemd_services/AniDownloader.service" > "$SYSTEMD_DIR/AniDownloader.service"

    cp -f "systemd_services/AniDownloader.timer" "$SYSTEMD_DIR/"

    if $WITH_WEB; then
        sed "s|%h/.local/bin/$BIN_NAME|$INSTALL_DIR/$BIN_NAME|g" \
            "systemd_services/AniDownloaderWeb.service" > "$SYSTEMD_DIR/AniDownloaderWeb.service"
    fi
fi

systemctl --user daemon-reload

if $WITH_TIMER; then
    systemctl --user enable --now AniDownloader.timer
    echo "Timer automatico attivato: controlla nuovi episodi ogni 15 minuti"
fi

if $WITH_WEB; then
    systemctl --user enable --now AniDownloaderWeb.service
    echo "Server web attivato: http://localhost:8989"
fi

# ──────────────────────────────────────────────
# 10. Uninstaller
# ──────────────────────────────────────────────
cp -f uninstall.sh "$INSTALL_DIR/uninstall.sh"
chmod +x "$INSTALL_DIR/uninstall.sh"

# ──────────────────────────────────────────────
# 11. Riepilogo
# ──────────────────────────────────────────────
echo ""
echo "╔═══════════════════════════════════════════════╗"
echo "║  Installazione completata!                    ║"
echo "╚═══════════════════════════════════════════════╝"
echo ""
echo "  Binario:       $INSTALL_DIR/$BIN_NAME"
echo "  Config:        $CONFIG_DIR"
echo echo ""
if $WITH_TIMER; then
    echo "  Timer:         attivo (ogni 15 min)"
fi
if $WITH_WEB; then
    echo "  Web UI:        http://localhost:8989"
fi
echo ""
echo "  Disinstallare: $INSTALL_DIR/uninstall.sh"
echo ""
