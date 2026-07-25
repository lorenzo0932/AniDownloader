#!/bin/bash
set -euo pipefail

APP_NAME="AniDownloader"
BIN_NAME="AniDownloader"
INSTALL_DIR="$HOME/.local/bin"
APP_DIR="$HOME/.local/share/applications"
SYSTEMD_DIR="$HOME/.config/systemd/user"
ICON_DEST_DIR="$HOME/.local/share/icons"
ICON_NAME="anidownloader_logo.png"
CONFIG_DIR="$HOME/.config/$APP_NAME"
CACHE_DIR="$HOME/.cache/$APP_NAME"

# ──────────────────────────────────────────────
# 1. Menu
# ──────────────────────────────────────────────
clear 2>/dev/null || true
cat << "EOF"
╔══════════════════════════════════════════════════════════════╗
║               AniDownloader — Uninstaller                    ║
╚══════════════════════════════════════════════════════════════╝

EOF

echo "Scegli cosa rimuovere:"
echo ""
echo "  1) Solo il binario"
echo "     Rimuove l'eseguibile, shortcut .desktop e icona."
echo "     Mantiene: servizi systemd, configurazione (config.json,"
echo "     series_data.json)."
echo "     Utile per reinstallare senza perdere dati."
echo ""
echo "  2) Solo i servizi"
echo "     Disabilita e rimuove i file systemd (timer e web)."
echo "     Mantiene: binario e configurazione."
echo "     Utile se non vuoi piu' l'esecuzione automatica."
echo ""
echo "  3) Tutto"
echo "     Rimuove binario, servizi, shortcut, icona,"
echo "     configurazione (config.json, series_data.json)."
echo "     ⚠️  I dati sono IRRECUPERABILI senza backup."
echo ""
echo "  0) Annulla"
echo ""

read -r -p "Scelta [0-3]: " choice
echo ""

case "$choice" in
    0) echo "Annullato."; exit 0 ;;
    1) MODE="binary" ;;
    2) MODE="services" ;;
    3) MODE="all" ;;
    *) echo "Scelta non valida."; exit 1 ;;
esac

# ──────────────────────────────────────────────
# 2. Esecuzione
# ──────────────────────────────────────────────

FERMA_SERVIZI=false
RIMUOVI_BINARIO=false
RIMUOVI_CONFIG=false

if [ "$MODE" = "services" ] || [ "$MODE" = "all" ]; then
    FERMA_SERVIZI=true
fi
if [ "$MODE" = "binary" ] || [ "$MODE" = "all" ]; then
    RIMUOVI_BINARIO=true
fi
if [ "$MODE" = "all" ]; then
    RIMUOVI_CONFIG=true
fi

# Ferma e disabilita servizi
if $FERMA_SERVIZI; then
    echo "Fermo e disabilito servizi systemd..."
    for svc in AniDownloader.timer AniDownloader.service AniDownloaderWeb.service; do
        systemctl --user stop "$svc" 2>/dev/null || true
        systemctl --user disable "$svc" 2>/dev/null || true
    done
    rm -f "$SYSTEMD_DIR/AniDownloader.service" \
          "$SYSTEMD_DIR/AniDownloader.timer" \
          "$SYSTEMD_DIR/AniDownloaderWeb.service"
    systemctl --user daemon-reload
    echo "  File systemd rimossi"
fi

# Rimuovi binario
if $RIMUOVI_BINARIO; then
    echo "Rimuovo eseguibile e uninstaller..."
    rm -f "$INSTALL_DIR/$BIN_NAME"
    rm -f "$INSTALL_DIR/uninstall.sh"

    echo "Rimuovo icona..."
    rm -f "$ICON_DEST_DIR/$ICON_NAME"

    echo "Rimuovo shortcut .desktop..."
    rm -f "$APP_DIR/$APP_NAME.desktop"
    rm -f "$APP_DIR/${APP_NAME}GUI.desktop"
    rm -f "$APP_DIR/${APP_NAME}Web.desktop"
    update-desktop-database "$APP_DIR" 2>/dev/null || true
fi

# Rimuovi configurazione
if $RIMUOVI_CONFIG; then
    echo "Rimuovo configurazione..."
    rm -rf "$CONFIG_DIR"
    rm -rf "$CACHE_DIR"
fi

# ──────────────────────────────────────────────
# 3. Riepilogo
# ──────────────────────────────────────────────
echo ""
echo "╔═══════════════════════════════════════════════╗"
echo "║  Disinstallazione completata!                 ║"
echo "╚═══════════════════════════════════════════════╝"
echo ""
if ! $FERMA_SERVIZI && ! $RIMUOVI_BINARIO; then
    echo "  (nulla rimosso — scelta non valida?)"
fi
if $FERMA_SERVIZI; then
    echo "  Servizi systemd: rimossi"
fi
if $RIMUOVI_BINARIO; then
    echo "  Binario:         rimosso ($INSTALL_DIR/$BIN_NAME)"
fi
if ! $RIMUOVI_CONFIG; then
    echo "  Configurazione:  mantenuta ($CONFIG_DIR)"
fi
echo ""
