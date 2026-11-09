#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace Config {
    namespace fs = std::filesystem;

    class PathHelper {
      public:
        static const std::string APP_NAME;

        static fs::path getConfigDir();
        static fs::path getLogDir();
        // Cache dell'applicazione su disco: XDG_CACHE_HOME/~/.cache (Linux),
        // %LOCALAPPDATA%\AniDownloader\Cache (Windows),
        // ~/Library/Caches (macOS).
        static fs::path getCacheDir();
        // Cache miniature poster: getCacheDir() / "thumbs".
        static fs::path getThumbCacheDir();
        // Radici RAM per i workdir (Linux/BSD): da usare in ordine, solo se la
        // base esiste. Vuoto su Windows e macOS. Non crea directory.
        static std::vector<fs::path> getRamWorkDirs();

        static fs::path getSeriesJsonPath();
        static fs::path getAppConfigPath();
        static fs::path getLogFilePath();
    };
} // namespace Config