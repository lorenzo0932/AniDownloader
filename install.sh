#!/bin/bash

# --- CONFIGURAZIONE PERCORSI ---
APP_NAME="AniDownloader"
BIN_NAME="AniDownloader" # Deve coincidere con il nome nel CMake
INSTALL_DIR="$HOME/.local/bin"
APP_DIR="$HOME/.local/share/applications"
SYSTEMD_DIR="$HOME/.config/systemd/user"
ICON_DEST_DIR="$HOME/.local/share/icons"
ICON_NAME="anidownloader_logo.png"
ICON_FULL_PATH="$ICON_DEST_DIR/$ICON_NAME"

echo "🚀 Inizio installazione di $APP_NAME..."

# 0. Ferma i servizi per evitare l'errore "Text file busy"
echo "🛑 Fermo eventuali servizi in esecuzione..."
systemctl --user stop AniDownloader.timer AniDownloader.service 2>/dev/null

# 1. Compilazione
echo "📦 Compilazione in corso..."
BUILD_DIR="build"
if command -v ninja &>/dev/null; then
    GENERATOR="Ninja"
    BUILD_CMD="ninja"
else
    GENERATOR="Unix Makefiles"
    BUILD_CMD="make -j$(nproc)"
fi
# Rimuovi cache CMake se il generatore è cambiato
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    CURRENT_GEN=$(grep CMAKE_MAKE_PROGRAM "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | head -1)
    if echo "$CURRENT_GEN" | grep -q "ninja" && [ "$GENERATOR" = "Unix Makefiles" ]; then
        rm -rf "$BUILD_DIR"
    elif echo "$CURRENT_GEN" | grep -q "make" && [ "$GENERATOR" = "Ninja" ]; then
        rm -rf "$BUILD_DIR"
    fi
fi
mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR"
cmake .. -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Release
$BUILD_CMD
if [ $? -ne 0 ]; then
    echo "❌ Errore durante la compilazione. Verifica le dipendenze."
    exit 1
fi

# 2. Creazione directory necessarie
mkdir -p "$INSTALL_DIR"
mkdir -p "$APP_DIR"
mkdir -p "$SYSTEMD_DIR"
mkdir -p "$ICON_DEST_DIR"

# 3. Installazione Binario
echo "📂 Installazione binario in $INSTALL_DIR..."
# Rimuovo prima il vecchio binario per evitare blocchi se è in uso
rm -f "$INSTALL_DIR/$BIN_NAME"
cp -f "$BIN_NAME" "$INSTALL_DIR/"
chmod +x "$INSTALL_DIR/$BIN_NAME"

# Torno alla root del progetto prima di cercare le altre cartelle
cd .. 

# 4. Gestione Icona
echo "🖼️ Installazione icona..."
if [ -f "resources/logo.png" ]; then
    cp -f "resources/logo.png" "$ICON_FULL_PATH"
    echo "✅ Icona copiata in $ICON_FULL_PATH"
else
    echo "⚠️ Attenzione: resources/logo.png non trovata (L'app non avrà icona)."
fi

# 5. Creazione file .desktop da zero (sovrascrittura garantita)
echo "⚙️ Configurazione file .desktop..."
rm -f "$APP_DIR/AniDownloader.desktop"
rm -f "$APP_DIR/AniDownloaderGUI.desktop"

cat <<EOF > "$APP_DIR/AniDownloader.desktop"
[Desktop Entry]
Version=1.0
Type=Application
Name=AniDownloaderC++
Comment=Download e Conversione Anime (C++)
Exec=bash -c "$INSTALL_DIR/$BIN_NAME --burst"
Path=$HOME
Icon=$ICON_FULL_PATH
Terminal=true
Categories=Network;Video;AudioVideo;
EOF

cat <<EOF > "$APP_DIR/AniDownloaderGUI.desktop"
[Desktop Entry]
Version=1.0
Type=Application
Name=AniDownloaderGUI
Comment=Download e Conversione Anime (C++)
Exec=bash -c "$INSTALL_DIR/$BIN_NAME --gui"
Path=$HOME
Icon=$ICON_FULL_PATH
Terminal=false
Categories=Network;Video;AudioVideo;
EOF


echo "✅ File .desktop generati e salvati in $APP_DIR"

# 6. Fix e installazione file Systemd
echo "⚙️ Configurazione file Systemd..."
if [ -d "systemd_services" ]; then
    sed "s|^ExecStart=.*|ExecStart=$INSTALL_DIR/$BIN_NAME|" \
        "systemd_services/AniDownloader.service" > "$SYSTEMD_DIR/AniDownloader.service"
    cp -f "systemd_services/AniDownloader.timer" "$SYSTEMD_DIR/"
    echo "✅ File systemd installati."
else
    echo "❌ Errore: Cartella systemd_services non trovata!"
fi

# 7. Attivazione Servizi e Aggiornamento Menu
echo "🔄 Ricarica demone systemd e riavvio timer..."
systemctl --user daemon-reload
systemctl --user enable --now AniDownloader.timer

# Aggiorna il database delle app per far apparire subito l'icona nel menu
update-desktop-database "$APP_DIR" 2>/dev/null

# 8. Installa uninstaller
echo "📂 Installazione uninstaller..."
cp -f uninstall.sh "$INSTALL_DIR/uninstall.sh"
chmod +x "$INSTALL_DIR/uninstall.sh"
echo "✅ Uninstaller disponibile in $INSTALL_DIR/uninstall.sh"

echo "---"
echo "✅ Installazione completata con successo!"
echo "💡 Se non vedi subito l'icona o la modifica nel menu, prova a disconnetterti e riconnetterti (Logout)."
echo "🕒 Il servizio automatico in background è attivo."