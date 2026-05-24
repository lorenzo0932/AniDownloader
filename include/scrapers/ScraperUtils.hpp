#pragma once
#include <string>
#include <filesystem>

namespace Core {
    class ScraperUtils {
    public:
        static std::string expandTilde(const std::string& path);
        static int getNextEpisodeNum(const std::string& seriesPath);
        static std::string generateFilename(const std::string& downloadUrl, int episodeNumber);
    };
}