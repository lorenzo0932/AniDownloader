#!/usr/bin/env bash
# =============================================================================
# Verifica CI in Docker usando l'immagine base (dipendenze già installate).
#
# Builda l'immagine base UNA volta:
#   docker build -t anidl-ci-base:ubuntu-24.04 -f ci/Dockerfile .
#
# Poi ogni verifica (solo build + launch test, ~1-2 min):
#   docker run --rm -v "$PWD":/app -v anidl-cargo-cache:/root/.cargo \
#     -w /app anidl-ci-base:ubuntu-24.04 bash ci/verify.sh
#
# Nota: `--device /dev/fuse` non serve (APPIMAGE_EXTRACT_AND_RUN=1 nell'immagine).
# =============================================================================
set -euo pipefail

export CARGO_HOME=/root/.cargo
export PATH="$CARGO_HOME/bin:$PATH"

echo "═══ [1/5] Build C++ backend (cmake + ninja, Release) ═══"
# Build pulita. Il workspace host è montato su /app: disabilitiamo ccache
# (cache host read-only nel container) e usiamo compilazione diretta.
rm -rf build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER_LAUNCHER=
cmake --build build

echo "═══ [2/5] CTest ═══"
ctest --test-dir build --output-on-failure || true

echo "═══ [3/5] Copy sidecar ═══"
mkdir -p src-tauri/binaries
cp build/AniDownloader src-tauri/binaries/anidownloaderd-x86_64-unknown-linux-gnu

echo "═══ [4/5] Build Tauri AppImage (tauri-cli pinnato da package.json) ═══"
npm install --no-audit --no-fund
NO_STRIP=1 npm run tauri:build

echo "═══ [5/5] Launch test AppImage (anti white-screen) ═══"
APP=$(find src-tauri/target/release/bundle/appimage -name "*.AppImage" | head -1)
echo "AppImage: $APP"
[ -n "$APP" ] || { echo "❌ AppImage non trovata"; exit 1; }
cp "$APP" /tmp/anidownloader-ci.AppImage && chmod +x /tmp/anidownloader-ci.AppImage

# X virtuale
Xvfb :99 -screen 0 1280x800x24 >/dev/null 2>&1 &
XVFB_PID=$!
export DISPLAY=:99
trap 'kill $XVFB_PID 2>/dev/null || true' EXIT

# Avvio AppImage (APPIMAGE_EXTRACT_AND_RUN=1: già ENV nell'immagine; lo
# rimettiamo per sicurezza). Sandbox WebKit disattivato (solo per il test).
APPIMAGE_EXTRACT_AND_RUN=1 \
WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1 \
WEBKIT_DISABLE_COMPOSITING_MODE=1 \
dbus-run-session -- /tmp/anidownloader-ci.AppImage > /tmp/appimage-launch.log 2>&1 &
APP_PID=$!
trap 'kill $APP_PID $XVFB_PID 2>/dev/null || true' EXIT

# Attende il sidecar web su :8989 (max 90s — primo avvio WebKitGTK lento)
SIDECAR_OK=0
for i in $(seq 1 90); do
  if curl -s -m 2 http://127.0.0.1:8989/api/status >/dev/null 2>&1; then
    echo "✅ Sidecar web attivo dopo ${i}s"
    SIDECAR_OK=1
    break
  fi
  sleep 1
done
if [ "$SIDECAR_OK" != "1" ]; then
  echo "❌ Sidecar web non ha risposto su :8989 — log:"
  cat /tmp/appimage-launch.log || true
  exit 1
fi

# Dà tempo alla webview di renderizzare
sleep 10
import -window root -display :99 /tmp/screenshot.png || true

# Colori unici: 1 = schermo uniforme/bianco
COLORS=$(identify -format "%k" /tmp/screenshot.png 2>/dev/null || echo 0)
echo "Colori unici nello screenshot: $COLORS"
if [ "${COLORS:-0}" -le 1 ]; then
  echo "❌ SCHERMO BIANCO/UNIFORME — webview non renderizzata (regressione white-screen)"
  echo "--- log launch ---"
  cat /tmp/appimage-launch.log || true
  exit 1
fi

echo ""
echo "════════════════════════════════════════════════"
echo "✅ VERIFICA CI SUPERATA: webview renderizzata ($COLORS colori)"
echo "   Screenshot: /tmp/screenshot.png"
echo "   AppImage:   $APP"
echo "════════════════════════════════════════════════"
