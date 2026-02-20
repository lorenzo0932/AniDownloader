#pragma once
#include <string>
#include <filesystem>

namespace Config {
    namespace fs = std::filesystem;

    // Definiamo i percorsi di default
    const fs::path DEFAULT_APP_CONFIG_PATH = fs::path(getenv("HOME")) / ".config/anidownloader/app_config.json";
    const fs::path DEFAULT_SERIES_JSON_PATH = fs::path(getenv("HOME")) / ".config/anidownloader/series.json";
    const fs::path DEFAULT_OUTPUT_DIR = fs::path(getenv("HOME")) / "Videos/AniDownloader";
    const fs::path DEFAULT_LOG_FILE = fs::path(getenv("HOME")) / ".config/anidownloader/anidownloader.log";
    
    const int DEFAULT_NUM_CHUNKS = 1;
}