#pragma once
#include <filesystem>
#include <string>

namespace Config {
    namespace fs = std::filesystem;

    class PathHelper {
    public:
        static const std::string APP_NAME;

        static fs::path getConfigDir();
        static fs::path getLogDir();
        static fs::path getVideosDir();

        static fs::path getSeriesJsonPath();
        static fs::path getAppConfigPath();
        static fs::path getLogFilePath();
    };
}