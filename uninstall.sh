#!/bin/bash
set -euo pipefail

APP_NAME="AniDownloader"
INSTALL_DIR="$HOME/.local/bin"
APP_DIR="$HOME/.local/share/applications"
SYSTEMD_DIR="$HOME/.config/systemd/user"
ICON_DEST_DIR="$HOME/.local/share/icons"
ICON_NAME="anidownloader_logo.png"
CONFIG_DIR="$HOME/.config/$APP_NAME"
CACHE_DIR="$HOME/.cache/$APP_NAME"
HEADLESS_DIR="$HOME/.local/share/anidownloader-headless"

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
echo "  1) Solo i binari e launcher"
echo "     Rimuove AppImage (legacy), anidownloaderd, il Flatpak desktop,"
echo "     shortcut .desktop, icona e script helper."
echo "     Mantiene: servizi systemd e configurazione (config.json, series_data.json)."
echo "     Utile per reinstallare senza perdere dati."
echo ""
echo "  2) Solo i servizi"
echo "     Disabilita e rimuove i file systemd (daemon e check timer)."
echo "     Mantiene: binari e configurazione."
echo "     Utile se non vuoi piu' l'esecuzione automatica."
echo ""
echo "  3) Tutto"
echo "     Rimuove binari, servizi, shortcut, icona, helper e"
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

# Ferma e disabilita servizi (nuovi e vecchi per retrocompatibilità)
if $FERMA_SERVIZI; then
    echo "Fermo e disabilito servizi systemd..."
    for svc in anidownloaderd.service anidownloader-check.service anidownloader-check.timer AniDownloader.timer AniDownloader.service AniDownloaderWeb.service; do
        systemctl --user stop "$svc" 2>/dev/null || true
        systemctl --user disable "$svc" 2>/dev/null || true
    done
    rm -f "$SYSTEMD_DIR/anidownloaderd.service" \
          "$SYSTEMD_DIR/anidownloader-check.service" \
          "$SYSTEMD_DIR/anidownloader-check.timer" \
          "$SYSTEMD_DIR/AniDownloader.service" \
          "$SYSTEMD_DIR/AniDownloader.timer" \
          "$SYSTEMD_DIR/AniDownloaderWeb.service"
    systemctl --user daemon-reload
    echo "  File systemd rimossi"
fi

# Rimuovi binari, shortcut e helper
if $RIMUOVI_BINARIO; then
    echo "Rimuovo eseguibili, script helper e uninstaller..."
    rm -f "$INSTALL_DIR/$APP_NAME.AppImage"
    rm -f "$INSTALL_DIR/$APP_NAME"
    rm -f "$INSTALL_DIR/AniDownloader"
    rm -f "$INSTALL_DIR/anidownloaderd"
    rm -f "$INSTALL_DIR/anidownloader-webui.sh"
    rm -f "$INSTALL_DIR/uninstall.sh"
    rm -f "$INSTALL_DIR/$APP_NAME.flatpak"
    rm -rf "$HEADLESS_DIR"

    # Desktop Flatpak (canale Linux unico)
    if command -v flatpak &>/dev/null; then
        echo "Rimuovo Flatpak desktop..."
        flatpak uninstall -y com.anidownloader.desktop 2>/dev/null || true
    fi

    echo "Rimuovo icona..."
    rm -f "$ICON_DEST_DIR/$ICON_NAME"

    echo "Rimuovo shortcut .desktop..."
    rm -f "$APP_DIR/$APP_NAME.desktop"
    rm -f "$APP_DIR/${APP_NAME}-CLI.desktop"
    rm -f "$APP_DIR/${APP_NAME}-Web.desktop"
    # Pulizia vecchie nomenclature
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
    echo "  Binari/Desktop:  rimossi ($INSTALL_DIR/$APP_NAME* e $HEADLESS_DIR)"
fi
if ! $RIMUOVI_CONFIG; then
    echo "  Configurazione:  mantenuta ($CONFIG_DIR)"
fi
echo ""
