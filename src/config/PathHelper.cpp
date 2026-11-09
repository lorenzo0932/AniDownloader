#include "config/PathHelper.hpp"
#include <cstdlib>
#include <system_error>

namespace Config {
    const std::string PathHelper::APP_NAME = "AniDownloader";

    static fs::path getHome() {
#ifdef _WIN32
        const char* home = std::getenv("USERPROFILE");
        if (home)
            return fs::path(home);
        const char* drive = std::getenv("HOMEDRIVE");
        const char* path = std::getenv("HOMEPATH");
        if (drive && path)
            return fs::path(drive) / path;
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
#elif defined(__APPLE__)
        return getHome() / "Library" / "Application Support" / APP_NAME;
#else
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        return xdg ? fs::path(xdg) / APP_NAME : getHome() / ".config" / APP_NAME;
#endif
    }

    fs::path PathHelper::getLogDir() {
#ifdef _WIN32
        const char* localappdata = std::getenv("LOCALAPPDATA");
        return localappdata ? fs::path(localappdata) / APP_NAME : getHome() / APP_NAME;
#elif defined(__APPLE__)
        return getHome() / "Library" / "Logs" / APP_NAME;
#else
        const char* xdg = std::getenv("XDG_CACHE_HOME");
        return xdg ? fs::path(xdg) / APP_NAME : getHome() / ".cache" / APP_NAME;
#endif
    }

    fs::path PathHelper::getVideosDir() { return getHome() / "Videos" / "Convertiti"; }

    fs::path PathHelper::getCacheDir() {
#ifdef _WIN32
        const char* localappdata = std::getenv("LOCALAPPDATA");
        fs::path base = localappdata ? fs::path(localappdata) : getHome() / "AppData" / "Local";
        return base / APP_NAME / "Cache";
#elif defined(__APPLE__)
        return getHome() / "Library" / "Caches" / APP_NAME;
#else
        const char* xdg = std::getenv("XDG_CACHE_HOME");
        return xdg ? fs::path(xdg) / APP_NAME : getHome() / ".cache" / APP_NAME;
#endif
    }

    fs::path PathHelper::getThumbCacheDir() { return getCacheDir() / "thumbs"; }

    std::vector<fs::path> PathHelper::getRamWorkDirs() {
        std::vector<fs::path> dirs;
#if !defined(_WIN32) && !defined(__APPLE__)
        // Solo basi esistenti e directory, nell'ordine di preferenza; nessuna
        // directory viene creata qui (lo fa chi usa i workdir).
        auto addIfBaseExists = [&dirs](const fs::path& base) {
            std::error_code ec;
            if (base.empty() || !fs::is_directory(base, ec) || ec)
                return;
            fs::path candidate = base / PathHelper::APP_NAME / "work";
            for (const auto& existing : dirs) {
                if (existing == candidate)
                    return;
            }
            dirs.push_back(std::move(candidate));
        };
        if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"))
            addIfBaseExists(fs::path(xdg));
        addIfBaseExists(fs::path("/dev/shm"));
#endif
        return dirs;
    }

    fs::path PathHelper::getSeriesJsonPath() { return getConfigDir() / "series_data.json"; }
    fs::path PathHelper::getAppConfigPath() { return getConfigDir() / "config.json"; }
    fs::path PathHelper::getLogFilePath() { return getLogDir() / "serie_critical_errors.log"; }
} // namespace Config