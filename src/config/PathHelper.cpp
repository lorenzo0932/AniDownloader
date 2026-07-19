#include "config/PathHelper.hpp"
#include <cstdlib>

namespace Config {
const std::string PathHelper::APP_NAME = "AniDownloader";

static fs::path getHome() {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
    if (home) return fs::path(home);
    const char* drive = std::getenv("HOMEDRIVE");
    const char* path = std::getenv("HOMEPATH");
    if (drive && path) return fs::path(drive) / path;
    return fs::path("C:\\");
#else
    const char* home = std::getenv("HOME");
    return home ? fs::path(home) : fs::path("/");
#endif
}

fs::path PathHelper::getConfigDir() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    return appdata ? fs::path(appdata) / APP_NAME : getHome() / APP_NAME;
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    return xdg ? fs::path(xdg) / APP_NAME : getHome() / ".config" / APP_NAME;
#endif
}

fs::path PathHelper::getLogDir() {
#ifdef _WIN32
    const char* localappdata = std::getenv("LOCALAPPDATA");
    return localappdata ? fs::path(localappdata) / APP_NAME : getHome() / APP_NAME;
#else
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    return xdg ? fs::path(xdg) / APP_NAME : getHome() / ".cache" / APP_NAME;
#endif
}

fs::path PathHelper::getVideosDir() {
    return getHome() / "Videos" / "Convertiti";
}

fs::path PathHelper::getSeriesJsonPath() { return getConfigDir() / "series_data.json"; }
fs::path PathHelper::getAppConfigPath() { return getConfigDir() / "config.json"; }
fs::path PathHelper::getLogFilePath() { return getLogDir() / "serie_critical_errors.log"; }
}