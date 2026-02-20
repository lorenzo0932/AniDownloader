#pragma once
#include <string>
#include <filesystem>

namespace Core {
    struct AppConfig {
        std::filesystem::path seriesJsonPath = "series.json";
        std::filesystem::path outputDir = "downloads";
        std::filesystem::path logFile = "anidownloader.log";
        bool convertToH265 = false;
        int numChunks = 1;
    };
}