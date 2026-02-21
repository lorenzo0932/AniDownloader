#!/bin/bash

# --- CONFIGURAZIONE PERCORSI ---
APP_NAME="AniDownloader"
BIN_NAME="AniDownloader" # Deve coincidere con il nome in add_executable nel CMake
INSTALL_DIR="$HOME/.local/bin"
APP_DIR="$HOME/.local/share/applications"
SYSTEMD_DIR="$HOME/.config/systemd/user"
ICON_DEST_DIR="$HOME/.local/share/icons"
ICON_NAME="anidownloader_logo.png"
ICON_FULL_PATH="$ICON_DEST_DIR/$ICON_NAME"

echo "🚀 Inizio installazione di $APP_NAME..."

# 1. Compilazione
echo "📦 Compilazione in corso con Ninja..."
mkdir -p build && cd build
cmake .. -G Ninja
ninja
if [ $? -ne 0 ]; then
    echo "❌ Errore durante la compilazione. Verifica le dipendenze (cpr, nlohmann_json)."
    exit 1
fi

# 2. Creazione directory necessarie
mkdir -p "$INSTALL_DIR"
mkdir -p "$APP_DIR"
mkdir -p "$SYSTEMD_DIR"
mkdir -p "$ICON_DEST_DIR"

# 3. Installazione Binario
echo "📂 Installazione binario in $INSTALL_DIR..."
cp "$BIN_NAME" "$INSTALL_DIR/"
chmod +x "$INSTALL_DIR/$BIN_NAME"

# 4. Gestione Icona (Il pezzo mancante!)
echo "🖼️ Installazione icona..."
# Cerchiamo l'icona nella cartella resources (adatta il path se è diverso)
if [ -f "../resources/logo.png" ]; then
    cp "../resources/logo.png" "$ICON_FULL_PATH"
    echo "✅ Icona copiata in $ICON_FULL_PATH"
else
    echo "⚠️ Attenzione: ../resources/logo.png non trovata. L'app non avrà un'icona personalizzata."
fi

# 5. Adattamento e installazione file di sistema
echo "⚙️ Configurazione file .desktop e systemd..."
cd .. # Torna alla root del progetto

# Fix percorso nel file .desktop (Exec e Icon)
sed -e "s|^Exec=.*|Exec=$INSTALL_DIR/$BIN_NAME|" \
    -e "s|^Icon=.*|Icon=$ICON_FULL_PATH|" \
    -e "s|^Path=.*|Path=$HOME|" \
    AniDownloader.desktop > "$APP_DIR/AniDownloader.desktop"

# Fix e copia file Systemd (dalla cartella systemd_services)
if [ -d "systemd_services" ]; then
    sed "s|^ExecStart=.*|ExecStart=$INSTALL_DIR/$BIN_NAME|" \
        "systemd_services/AniDownloader.service" > "$SYSTEMD_DIR/AniDownloader.service"
    cp "systemd_services/AniDownloader.timer" "$SYSTEMD_DIR/"
else
    echo "❌ Errore: Cartella systemd_services non trovata!"
fi

# 6. Attivazione Servizi
echo "🔄 Ricarica demone systemd e attivazione timer..."
systemctl --user daemon-reload
systemctl --user enable --now AniDownloader.timer

# Aggiorna il database dei file desktop per far apparire l'icona nel menu
update-desktop-database "$APP_DIR" 2>/dev/null

echo "---"
echo "✅ Installazione completata!"
echo "💡 Ora dovresti vedere l'icona nel tuo menu applicazioni."
echo "🕒 Il servizio automatico è attivo (ogni 15 min)."