# =============================================================================
# Immagine BASE per la verifica CI di AniDownloader (replica release.yml Linux)
#
# Contiene SOLO le dipendenze FISSE (che non cambiano tra un run e l'altro):
#   - pacchetti apt (build C++, WebKitGTK, aria2/ffmpeg, xvfb, launch test)
#   - Rust 1.96.0 pinnato (rustup, come rust-toolchain.toml)
#   - Node 22.22.2 + npm
#
# Si builda UNA volta:  docker build -t anidl-ci-base:ubuntu-24.04 -f ci/Dockerfile .
# Poi ogni verifica riusa l'immagine senza reinstallare nulla (vedi ci/verify.sh).
#
# Nota FUSE/AppImage: linuxdeploy è un'AppImage; nei container serve FUSE.
# Per eseguirlo senza /dev/fuse si usa APPIMAGE_EXTRACT_AND_RUN=1 (ENV qui sotto).
# =============================================================================
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV CARGO_HOME=/root/.cargo
ENV RUSTUP_HOME=/root/.rustup
ENV PATH="/root/.cargo/bin:/usr/local/bin:${PATH}"
# linuxdeploy (AppImage) nei container: estrai invece di montare (no FUSE).
ENV APPIMAGE_EXTRACT_AND_RUN=1

# ── 1. Pacchetti di sistema (stesso set di release.yml + tooling launch test) ──
RUN apt-get update -qq && apt-get install -y -qq \
    curl git ca-certificates build-essential \
    cmake ninja-build pkg-config \
    libcurl4-openssl-dev libssl-dev \
    libwebkit2gtk-4.1-dev libayatana-appindicator3-dev librsvg2-dev \
    patchelf \
    aria2 ffmpeg \
    xvfb dbus-x11 imagemagick \
    jq \
    libfuse2t64 \
    && rm -rf /var/lib/apt/lists/*

# ── 2. Rust 1.96.0 (pinnato — allineato a rust-toolchain.toml del repo) ──
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
        | sh -s -- -y --profile minimal --default-toolchain 1.96.0 \
    && rustup component add rustfmt clippy \
    && rustc --version && cargo --version

# ── 3. Node 22.22.2 (pinnato — allineato a setup-node in release.yml) ──
RUN curl -fsSL https://nodejs.org/dist/v22.22.2/node-v22.22.2-linux-x64.tar.xz \
        | tar -xJ -C /usr/local --strip-components=1 \
    && node --version && npm --version

WORKDIR /app
