# Tauri Migration — Todo (Completato ✅)

## Punto 1 ✅ — Branch legacy Qt
- Creare `legacy/qt-gui` da `dev` e pushare

## Punto 2 ✅ — Rimuovere Qt da feat/tauri
- `git rm -r include/gui/ src/gui/`
- CMakeLists.txt: Qt6 rimosso, AUTOMOC rimosso, file gui rimossi
- main.cpp: QApplication, QSurfaceFormat, guiMode rimossi
- install.sh/ps1: check Qt6 e windeployqt rimossi
- Build verificato (21/21, zero Qt)

## Punto 3 ✅ — Tauri init + sidecar + tray
- src-tauri/ con Cargo.toml, tauri.conf.json, main.rs
- Sidecar: externalBin, shell:allow-execute
- Tray icon con menu (Mostra/Nascondi/Esci)
- Plugin: shell, notification, dialog, fs
- CMakeLists.txt: target-triple detection + sidecar copy
- Icone generate da resources/logo.png

## Punto 4 ✅ — Build Tauri
- `npx @tauri-apps/cli build` riuscito
- Binary: src-tauri/target/release/anidownloader (17MB)
- .deb: AniDownloader_2.0.0_amd64.deb (5.8MB)
- AppImage: non generato per assenza FUSE (funzionerà su sistema reale)

## Punto 5 ✅ — AGENTS.md aggiornato
- Build instructions: C++ + Svelte + Tauri
- Branch strategy documentata
- Convenzioni aggiornate

## Da fare (opzionale)
- Comando IPC Rust per start/stop download (bypassa HTTP)
- api.js modificato per invoke() Tauri quando disponibile
- PWA (service worker + manifest.json) per --web mode
- Test su Windows (WebView2)
