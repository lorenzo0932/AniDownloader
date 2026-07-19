#!/bin/bash

# --- CONFIGURAZIONE PERCORSI ---
APP_NAME="AniDownloader"
BIN_NAME="AniDownloader"
INSTALL_DIR="$HOME/.local/bin"
APP_DIR="$HOME/.local/share/applications"
SYSTEMD_DIR="$HOME/.config/systemd/user"
ICON_DEST_DIR="$HOME/.local/share/icons"
ICON_NAME="anidownloader_logo.png"
CONFIG_DIR="$HOME/.config/$APP_NAME"
CACHE_DIR="$HOME/.cache/$APP_NAME"

echo "--- Disinstallazione $APP_NAME ---"

# 0. Domanda: mantenere la configurazione?
read -p "Mantenere la configurazione? (serie, config) [s/N]: " keep_config
keep_config=$(echo "$keep_config" | tr '[:upper:]' '[:lower:]')

# 1. Ferma e disabilita servizi
echo "Fermo servizi..."
systemctl --user stop "$APP_NAME.timer" "$APP_NAME.service" 2>/dev/null
systemctl --user disable "$APP_NAME.timer" "$APP_NAME.service" 2>/dev/null

# 2. Rimuovi eseguibile
echo "Rimuovo eseguibile..."
rm -f "$INSTALL_DIR/$BIN_NAME"
rm -f "$INSTALL_DIR/uninstall.sh"

# 3. Rimuovi icona
echo "Rimuovo icona..."
rm -f "$ICON_DEST_DIR/$ICON_NAME"

# 4. Rimuovi shortcut .desktop
echo "Rimuovo shortcut..."
rm -f "$APP_DIR/$APP_NAME.desktop"
rm -f "$APP_DIR/${APP_NAME}GUI.desktop"

# 5. Rimuovi file systemd
echo "Rimuovo file systemd..."
rm -f "$SYSTEMD_DIR/$APP_NAME.service"
rm -f "$SYSTEMD_DIR/$APP_NAME.timer"

# 6. Configurazione
if [ "$keep_config" = "s" ]; then
    echo "Configurazione mantenuta in $CONFIG_DIR"
else
    echo "Rimuovo configurazione..."
    rm -rf "$CONFIG_DIR"
    rm -rf "$CACHE_DIR"
fi

# 7. Ricarica systemd e aggiorna desktop
systemctl --user daemon-reload 2>/dev/null
update-desktop-database "$APP_DIR" 2>/dev/null

echo ""
echo "--- Disinstallazione completata! ---"
if [ "$keep_config" = "s" ]; then
    echo "La configurazione e' stata mantenuta."
    echo "  $CONFIG_DIR"
fi
