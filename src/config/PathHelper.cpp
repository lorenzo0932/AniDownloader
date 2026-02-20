#include "config/PathHelper.hpp"
#include <cstdlib>

namespace Config {
const std::string PathHelper::APP_NAME = "AniDownloader";

static fs::path getHome() {
    const char* home = std::getenv("HOME");
    return home ? fs::path(home) : fs::path("/");
}

fs::path PathHelper::getConfigDir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    return xdg ? fs::path(xdg) / APP_NAME : getHome() / ".config" / APP_NAME;
}

fs::path PathHelper::getLogDir() {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    return xdg ? fs::path(xdg) / APP_NAME : getHome() / ".cache" / APP_NAME;
}

fs::path PathHelper::getVideosDir() {
    return getHome() / "Videos" / "Convertiti";
}

fs::path PathHelper::getSeriesJsonPath() { return getConfigDir() / "series_data.json"; }
fs::path PathHelper::getAppConfigPath() { return getConfigDir() / "config.json"; }
fs::path PathHelper::getLogFilePath() { return getLogDir() / "serie_critical_errors.log"; }
}