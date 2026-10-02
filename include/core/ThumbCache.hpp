#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace Core {
    // Tier di larghezze (px) servite da GET /api/poster. Qualunque w
    // richiesto viene arrotondato al tier superiore (mai upscale oltre il
    // tier massimo: la scala usa min(iw,w), come il c_limit dei CDN).
    inline constexpr std::array<int, 5> THUMB_WIDTHS = {32, 96, 480, 720, 1080};
    constexpr int THUMB_DEFAULT_WIDTH = 480;
    constexpr int THUMB_DEFAULT_QUALITY = 80;
    // Tetto della cache miniature su disco, con eviction LRU per mtime
    // a ogni scrittura: niente crescita illimitata, niente manutenzione.
    constexpr uint64_t THUMB_CACHE_MAX_BYTES = 500ULL * 1024ULL * 1024ULL;

    // Arrotonda w al tier superiore (<=0 -> default, >max -> max). Pura.
    int selectThumbWidth(int requested);
    // Clamp 1..100 (fuori range -> default). Pura.
    int clampThumbQuality(int requested);
    // Chiave cache stabile (FNV-1a su path+mtime+size+w+q): se il poster
    // cambia (mtime/size) cambia la chiave, mai thumb stantii. Pura.
    std::string thumbCacheKey(const std::string& absPath, uint64_t mtime, uint64_t size, int w,
                              int q);
    // Genera (ffmpeg Lanczos -> WebP) o riusa dal disco. nullopt se il
    // poster manca o la generazione fallisce (il chiamante serve
    // l'originale come fallback).
    std::optional<std::string> thumbFor(const std::string& seriesPath, int w, int q);

    struct ThumbCacheInfo {
        uint64_t files = 0;
        uint64_t bytes = 0;
    };
    ThumbCacheInfo thumbCacheInfo();
    // Svuota la cache, ritorna cio' che ha rimosso.
    ThumbCacheInfo clearThumbCache();
    // Enforcement del tetto su directory arbitraria (per i test).
    // Rimuove i file piu' vecchi (mtime) finche' bytes <= maxBytes.
    ThumbCacheInfo enforceThumbCacheCap(const std::string& dir, uint64_t maxBytes);
} // namespace Core
