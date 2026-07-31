# Third-Party Notices

AniDownloader is licensed under the MIT License — see the `LICENSE` file.
This document lists the third-party components used to build AniDownloader,
together with their licenses and copyright holders. The full license texts
are in the `licenses/` directory.

## Components linked or embedded (built into the application)

| Component | Version | Source | License | Copyright |
|---|---|---|---|---|
| cpp-httplib (HTTP/WebSocket server) | v0.18.1 | https://github.com/yhirose/cpp-httplib | MIT | yhirose |
| cpr (HTTP client) | 1.10.5 | https://github.com/libcpr/cpr | MIT | cpr developers |
| nlohmann-json | 3.11.3 | https://github.com/nlohmann/json | MIT | Niels Lohmann and contributors |
| libcurl (system package) | distro-provided | https://curl.se | curl | Daniel Stenberg and contributors |
| OpenSSL / libcrypto (AES-256-GCM, PBKDF2) | distro-provided | https://www.openssl.org | Apache-2.0 | OpenSSL Software Foundation |
| Tauri (desktop wrapper) | 2.x | https://github.com/tauri-apps/tauri | MIT / Apache-2.0 | Tauri contributors |
| tauri-plugin-notification / dialog / fs / shell | 2.x | https://github.com/tauri-apps/plugins-workspace | MIT / Apache-2.0 | Tauri contributors |
| serde, serde_json | 1.x | https://github.com/serde-rs/serde | MIT / Apache-2.0 | serde developers |
| png | 0.17 | https://github.com/image-rs/image-png | MIT / Apache-2.0 | image-rs contributors |
| Svelte (frontend) | 5.x | https://github.com/sveltejs/svelte | MIT | Svelte contributors |
| Vite (build tool) | 6.x | https://github.com/vitejs/vite | MIT | Vite contributors |
| @sveltejs/vite-plugin-svelte | 5.x | https://github.com/sveltejs/vite-plugin-svelte | MIT | Svelte contributors |

## Runtime tools (not bundled, installed separately by the user)

These programs are invoked as external processes; they are NOT
distributed with AniDownloader.

| Component | Source | License | Copyright |
|---|---|---|---|
| aria2 (multi-thread download) | https://github.com/aria2/aria2 | GPL-2.0-or-later | aria2 project |
| FFmpeg / ffprobe (conversion and verification) | https://ffmpeg.org | LGPL-2.1-or-later (or GPL builds) | FFmpeg developers |

License texts for all components above are available in the `licenses/`
directory:

- `licenses/mit.txt`
- `licenses/apache-2.0.txt`
- `licenses/curl.txt`
- `licenses/gpl-2.0.txt`
- `licenses/lgpl-2.1.txt`
