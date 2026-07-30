# Build da sorgente

## Prerequisiti

### Minimi

```bash
# Debian/Ubuntu
sudo apt install cmake g++ libcurl4-openssl-dev libssl-dev nodejs npm

# Fedora
sudo dnf install cmake gcc-c++ libcurl-devel openssl-devel nodejs npm

# Arch Linux
sudo pacman -S cmake gcc curl openssl nodejs npm

# macOS (Homebrew)
brew install cmake node
```

### Per Tauri (app nativa desktop)

```bash
# Debian/Ubuntu
sudo apt install libwebkit2gtk-4.1-dev libappindicator-gtk3-dev

# Fedora
sudo dnf install webkit2gtk4.1-devel libappindicator-gtk3-devel

# Arch Linux
sudo pacman -S webkit2gtk-4.1 libappindicator-gtk3
```

### Runtime (richiesti per eseguire)

```bash
sudo apt install ffmpeg aria2
```

## Build standard

```bash
git clone https://github.com/lorenzo0932/AniDownloader.git
cd AniDownloader

# 1. Frontend Svelte
cd web && npm install && npm run build && cd ..

# 2. Backend C++
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 3. (opzionale) App nativa Tauri
npx @tauri-apps/cli build
```

Output: `build/AniDownloader`

## Opzioni CMake

| Opzione | Default | Descrizione |
|---------|---------|-------------|
| `CMAKE_BUILD_TYPE` | `Release` | `Release`, `Debug`, `RelWithDebInfo` |
| `-G Ninja` | (auto) | Usa Ninja invece di Make |

### Build di debug

```bash
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
```

## Cross-compilazione

### Per Tauri sidecar

Il CMakeLists.txt rileva automaticamente il target triple e copia il binario
in `src-tauri/binaries/` con il nome corretto per Tauri:

- Linux x86_64: `AniDownloader-x86_64-unknown-linux-gnu`
- Windows x86_64: `AniDownloader-x86_64-pc-windows-msvc.exe`
- macOS arm64: `AniDownloader-aarch64-apple-darwin`
- macOS x86_64: `AniDownloader-x86_64-apple-darwin`

## Troubleshooting

### `Python3 not found`

Il frontend non verrà embedded nel binario. Puoi comunque buildare il C++
e servire `web/dist/` staticamente (o copiare i file a lato).

### `OpenSSL not found`

```bash
# Debian/Ubuntu
sudo apt install libssl-dev

# Fedora
sudo dnf install openssl-devel

# macOS
brew install openssl
export OPENSSL_ROOT_DIR="/opt/homebrew/opt/openssl@3"
```

### `fatal error: nlohmann/json.hpp`

CMake FetchContent scarica automaticamente le dipendenze. Se hai problemi
di rete, scarica manualmente:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_JSON=/path/to/json
```

### ChromeDriver per scraping

AniDownloader cerca `chromedriver` nel PATH. Installalo:

```bash
# Debian/Ubuntu
sudo apt install chromium-chromedriver

# O scarica manualmente da https://googlechromelabs.github.io/chrome-for-testing/
```
