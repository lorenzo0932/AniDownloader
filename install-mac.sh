#!/bin/bash
# install-mac.sh — AniDownloader headless su macOS (LaunchAgent utente).
#
# Installa il demone (web + scheduler interno) come LaunchAgent dell'utente:
# niente sudo, niente timer OS (i controlli sono interni al demone, ADR-004).
# L'app desktop resta il .dmg Tauri; questo script copre il solo headless.
set -euo pipefail

APP_NAME="AniDownloader"
REPO="lorenzo0932/AniDownloader"
LABEL="com.anidownloader.daemon"
PREFIX="$HOME/.local"
BIN_DIR="$PREFIX/bin"
HEADLESS_DIR="$PREFIX/share/anidownloader-headless"
PLIST_DIR="$HOME/Library/LaunchAgents"
PLIST="$PLIST_DIR/$LABEL.plist"
LOG_DIR="$HOME/Library/Logs/$APP_NAME"
CONFIG_DIR="$HOME/Library/Application Support/$APP_NAME"
CACHE_DIR="$HOME/Library/Caches/$APP_NAME"

MODE="install"
VERSION=""
FORCE_LOCAL=false
FORCE_DEV=false
PURGE=false

for arg in "$@"; do
    case "$arg" in
        --uninstall) MODE="uninstall" ;;
        --status) MODE="status" ;;
        --local) FORCE_LOCAL=true ;;
        --dev) FORCE_DEV=true ;;
        --purge) PURGE=true ;;
        -h|--help) MODE="help" ;;
        *) VERSION="$arg" ;;
    esac
done

get_latest_release() {
    curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" | \
        grep '"tag_name"' | cut -d'"' -f4
}

# Ultima prerelease semver (canale dev, tag vX.Y.Z-dev.N).
get_latest_dev() {
    curl -fsSL "https://api.github.com/repos/$REPO/releases?per_page=100" | \
        grep -oE '"tag_name": "v?[0-9]+\.[0-9]+\.[0-9]+-[0-9A-Za-z.-]+' | \
        cut -d'"' -f4 | sort -V | tail -1
}

detect_arch() {
    case "$(uname -m)" in
        arm64|aarch64) echo "aarch64" ;;
        x86_64) echo "x86_64" ;;
        *) echo "unsupported" ;;
    esac
}

print_help() {
    cat <<EOF
AniDownloader — installazione headless macOS

Uso:
  ./install-mac.sh                 installa l'ultima release stabile
  ./install-mac.sh --dev           installa l'ultima prerelease (fallback stabile)
  ./install-mac.sh vX.Y.Z          installa una versione specifica
  ./install-mac.sh --local         build locale dal checkout corrente
  ./install-mac.sh --status        stato del LaunchAgent e del demone
  ./install-mac.sh --uninstall     rimuove LaunchAgent e binario
  ./install-mac.sh --uninstall --purge   rimuove anche config, cache e dati

File installati:
  $HEADLESS_DIR/anidownloaderd
  $BIN_DIR/anidownloaderd            (symlink)
  $PLIST                             (LaunchAgent)
  $LOG_DIR/daemon.out.log, daemon.err.log

Requisiti runtime: ffmpeg e aria2c (brew install ffmpeg aria2).
EOF
}

require_macos() {
    if [ "$(uname -s)" != "Darwin" ]; then
        echo "Questo script è per macOS (rilevato: $(uname -s))." >&2
        echo "Su Linux usa install.sh; su Windows install.ps1." >&2
        exit 1
    fi
}

uid_now() { id -u; }

launchctl_bootout() {
    launchctl bootout "gui/$(uid_now)/$LABEL" 2>/dev/null || \
        launchctl bootout "gui/$(uid_now)" "$PLIST" 2>/dev/null || \
        launchctl unload "$PLIST" 2>/dev/null || true
}

launchctl_bootstrap() {
    if launchctl bootstrap "gui/$(uid_now)" "$PLIST" 2>/dev/null; then
        launchctl kickstart -k "gui/$(uid_now)/$LABEL" 2>/dev/null || true
    else
        # macOS più vecchi senza bootstrap/bootout.
        launchctl load -w "$PLIST"
    fi
}

write_plist() {
    mkdir -p "$PLIST_DIR" "$LOG_DIR"
    cat > "$PLIST" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<!-- Generato da install-mac.sh: percorsi reali dell'utente, log in ~/Library/Logs. -->
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>$LABEL</string>
    <key>ProgramArguments</key>
    <array>
        <string>$BIN_DIR/anidownloaderd</string>
        <string>--web</string>
        <string>--silent</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>StandardOutPath</key>
    <string>$LOG_DIR/daemon.out.log</string>
    <key>StandardErrorPath</key>
    <string>$LOG_DIR/daemon.err.log</string>
    <key>WorkingDirectory</key>
    <string>$HOME</string>
    <key>EnvironmentVariables</key>
    <dict>
        <key>PATH</key>
        <string>/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin</string>
    </dict>
</dict>
</plist>
PLIST
    plutil -lint "$PLIST" >/dev/null
}

install_binary_from_release() {
    local arch asset tmp
    arch="$(detect_arch)"
    if [ "$arch" = "unsupported" ]; then
        echo "Architettura non supportata: $(uname -m)" >&2
        return 1
    fi
    if [ "$arch" = "x86_64" ]; then
        echo "  Nota: le release pubblicano solo il pacchetto mac-aarch64." >&2
        echo "  Su Intel usa --local (build dal checkout)." >&2
        return 1
    fi
    asset="anidownloaderd-${VERSION}-mac-${arch}.tar.gz"
    tmp="$(mktemp -d)"
    echo "  Download: $asset"
    if ! curl -fsSL -o "$tmp/headless.tar.gz" \
        "https://github.com/$REPO/releases/download/$VERSION/$asset"; then
        rm -rf "$tmp"
        return 1
    fi
    tar -xzf "$tmp/headless.tar.gz" -C "$tmp"
    local bin
    bin="$(find "$tmp" -name anidownloaderd -type f | head -1)"
    if [ -z "$bin" ]; then
        echo "  Binario non trovato nell'archivio." >&2
        rm -rf "$tmp"
        return 1
    fi
    mkdir -p "$HEADLESS_DIR"
    install -m 0755 "$bin" "$HEADLESS_DIR/anidownloaderd"
    rm -rf "$tmp"
}

install_binary_local() {
    echo "  Build locale (frontend + backend)..."
    local root jobs
    root="$(cd "$(dirname "$0")" && pwd)"
    jobs="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
    [ "$jobs" -gt 32 ] && jobs=32
    (cd "$root/web" && npm install --no-audit --no-fund && npm run build)
    cmake -S "$root" -B "$root/build" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$root/build" --parallel "$jobs"
    mkdir -p "$HEADLESS_DIR"
    install -m 0755 "$root/build/AniDownloader" "$HEADLESS_DIR/anidownloaderd"
}

resolve_version() {
    if $FORCE_LOCAL; then
        return 0
    fi
    if [ -z "$VERSION" ]; then
        if $FORCE_DEV; then
            echo "Recupero ultima prerelease..."
            VERSION="$(get_latest_dev)" || true
            if [ -z "$VERSION" ]; then
                echo "  Nessuna prerelease, uso la stabile."
                VERSION="$(get_latest_release)" || true
            fi
        else
            echo "Recupero ultima release..."
            VERSION="$(get_latest_release)" || true
        fi
    fi
}

do_install() {
    require_macos
    resolve_version
    mkdir -p "$BIN_DIR" "$HEADLESS_DIR" "$CONFIG_DIR"

    launchctl_bootout
    pkill -f "$HEADLESS_DIR/anidownloaderd" 2>/dev/null || true

    if $FORCE_LOCAL || [ -z "$VERSION" ]; then
        install_binary_local
    else
        echo "Versione: $VERSION"
        if ! install_binary_from_release; then
            echo "  Download non riuscito, passo alla build locale."
            install_binary_local
        fi
    fi

    ln -sfn "$HEADLESS_DIR/anidownloaderd" "$BIN_DIR/anidownloaderd"
    write_plist
    launchctl_bootstrap

    echo ""
    echo "Installato: $LABEL"
    echo "  Binario: $BIN_DIR/anidownloaderd"
    echo "  Log:     $LOG_DIR/daemon.out.log"
    echo "  Web UI:  http://127.0.0.1:8989"
    echo "  Stato:   ./install-mac.sh --status"
    if ! command -v ffmpeg >/dev/null || ! command -v aria2c >/dev/null; then
        echo ""
        echo "  ATTENZIONE: ffmpeg e/o aria2c non trovati. Installa con:"
        echo "    brew install ffmpeg aria2"
    fi
}

do_uninstall() {
    require_macos
    launchctl_bootout
    rm -f "$PLIST"
    rm -f "$BIN_DIR/anidownloaderd"
    rm -rf "$HEADLESS_DIR"
    echo "LaunchAgent e binario rimossi."
    if $PURGE; then
        rm -rf "$CONFIG_DIR" "$CACHE_DIR" "$LOG_DIR"
        echo "Rimossi anche config, cache e log ($CONFIG_DIR, $CACHE_DIR, $LOG_DIR)."
    else
        echo "Config, cache e log conservati; usa --purge per rimuoverli."
    fi
}

do_status() {
    require_macos
    echo "LaunchAgent $LABEL:"
    if launchctl print "gui/$(uid_now)/$LABEL" 2>/dev/null | sed -n '1,12p'; then
        :
    else
        echo "  non caricato"
    fi
    echo ""
    echo "Demone (http://127.0.0.1:8989/api/status):"
    curl -s --max-time 3 http://127.0.0.1:8989/api/status || echo "  non raggiungibile"
    echo ""
    echo "Log: $LOG_DIR/daemon.err.log"
}

case "$MODE" in
    help) print_help ;;
    status) do_status ;;
    uninstall) do_uninstall ;;
    install) do_install ;;
esac
