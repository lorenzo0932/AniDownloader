#include "core/FileUtils.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <regex>
#ifdef __linux__
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <stdio.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace Core {

    namespace {

        // Cache per countVideoFiles: /api/series riscansionava tutte le directory a
        // ogni GET. Validazione con mtime della directory (1 syscall) + TTL 5s,
        // stesso pattern della cache ffprobe (MediaProbe). Eviction semplice: quando
        // la cache supera 256 voci viene rimossa la prima in ordine alfabetico.
        struct CountCacheEntry {
            int count = 0;
            std::filesystem::file_time_type dirMtime;
            std::chrono::steady_clock::time_point checkedAt;
        };

        std::mutex s_countCacheMutex;
        std::map<std::string, CountCacheEntry> s_countCache;
        constexpr size_t COUNT_CACHE_MAX = 256;
        constexpr auto COUNT_CACHE_TTL = std::chrono::seconds(5);

    } // namespace

    int countVideoFiles(const std::string& dirPath) {
        {
            std::lock_guard<std::mutex> lock(s_countCacheMutex);
            auto it = s_countCache.find(dirPath);
            if (it != s_countCache.end()) {
                const auto& e = it->second;
                std::error_code ec;
                auto currentMtime = std::filesystem::last_write_time(dirPath, ec);
                if (!ec && currentMtime == e.dirMtime &&
                    std::chrono::steady_clock::now() - e.checkedAt < COUNT_CACHE_TTL) {
                    return e.count;
                }
            }
        }

        int count = 0;
        std::filesystem::path dir(dirPath);
        if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
            return 0;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file())
                continue;
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".mp4" || ext == ".mkv" || ext == ".webm" || ext == ".avi" || ext == ".mov")
                count++;
        }

        std::error_code ec;
        auto dirMtime = std::filesystem::last_write_time(dir, ec);
        if (!ec) {
            std::lock_guard<std::mutex> lock(s_countCacheMutex);
            if (s_countCache.size() >= COUNT_CACHE_MAX)
                s_countCache.erase(s_countCache.begin());
            s_countCache[dirPath] = {count, dirMtime, std::chrono::steady_clock::now()};
        }
        return count;
    }

    std::string readNfoDescription(const std::string& seriesPath) {
        std::filesystem::path sPath(seriesPath);
        auto nfoPath = sPath.parent_path() / "tvshow.nfo";
        if (!std::filesystem::exists(nfoPath))
            nfoPath = sPath / "tvshow.nfo";
        if (!std::filesystem::exists(nfoPath))
            return {};

        std::ifstream f(nfoPath);
        std::string content((std::istreambuf_iterator<char>(f)), {});
        std::regex plotRe(R"(<plot[^>]*>([\s\S]*?)</plot>)", std::regex::icase);
        std::smatch m;
        if (!std::regex_search(content, m, plotRe) || m.size() <= 1)
            return {};
        std::string desc = m[1].str();
        desc = std::regex_replace(desc, std::regex("^\\s+|\\s+$"), "");
        return desc;
    }

    std::string findPosterPath(const std::string& seriesPath) {
        std::filesystem::path sPath(seriesPath);
        auto poster = sPath / "folder.jpg";
        if (std::filesystem::exists(poster))
            return poster.string();
        poster = sPath.parent_path() / "folder.jpg";
        if (std::filesystem::exists(poster))
            return poster.string();
        return {};
    }

    std::vector<DirEntry> listDirectories(const std::string& dirPath) {
        std::vector<DirEntry> entries;
        std::filesystem::path dp;

        if (dirPath == "~" || dirPath.rfind("~/", 0) == 0) {
            const char* home = std::getenv("HOME");
            if (!home) {
                dp = std::filesystem::path(dirPath);
            } else {
                dp = dirPath == "~" ? std::filesystem::path(home)
                                    : std::filesystem::path(home) / dirPath.substr(2);
            }
        } else {
            dp = std::filesystem::path(dirPath);
        }

        if (!std::filesystem::exists(dp) || !std::filesystem::is_directory(dp))
            return entries;

        for (const auto& entry : std::filesystem::directory_iterator(dp)) {
            if (!entry.is_directory())
                continue;
            auto filename = entry.path().filename().string();
            if (filename[0] == '.')
                continue;
            auto ftime = std::filesystem::last_write_time(entry);
            // Conversione portatile file_clock -> system_clock: l'epoch di file_time_type
            // dipende dalla piattaforma (libstdc++ usa 2174-01-01 -> mtime negativi).
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() +
                std::chrono::system_clock::now());
            auto mtime =
                std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
            entries.push_back({filename, entry.path().string(), mtime});
        }

        std::sort(entries.begin(), entries.end(),
                  [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });

        return entries;
    }

#if defined(__linux__) || defined(__APPLE__)
    // Fallback atomico no-replace con link()+unlink(): il file finale diventa
    // un hard link al temporaneo (stesso inode) e il temporaneo viene rimosso.
    // link() fallisce con EEXIST se la destinazione esiste: mai sovrascrittura.
    PublishStatus publishNoReplaceFallback(const std::string& tempPath,
                                           const std::string& finalPath) {
        if (::link(tempPath.c_str(), finalPath.c_str()) != 0) {
            if (errno == EEXIST) {
                return PublishStatus::Exists;
            }
            return PublishStatus::NoAtomicSupport;
        }
        // Il file finale (stesso inode) esiste già: la rimozione del temporaneo
        // è fattibile; un eventuale errore qui non compromette il risultato.
        ::unlink(tempPath.c_str());
        return PublishStatus::Success;
    }
#endif

    PublishStatus publishNoReplace(const std::string& tempPath, const std::string& finalPath) {
#if defined(__linux__) || defined(__APPLE__)
        int result = -1;
        int savedErrno = 0;
#ifdef __linux__
        // renameat2 con RENAME_NOREPLACE: atomico e fallisce se la destinazione
        // esiste. Disponibile su kernel >= 3.15; ENOSYS/EINVAL = filesystem che
        // non supporta il flag → fallback sottostante.
        result = ::syscall(SYS_renameat2, AT_FDCWD, tempPath.c_str(), AT_FDCWD, finalPath.c_str(),
                           RENAME_NOREPLACE);
        savedErrno = errno;
#else
        // renamex_np con RENAME_EXCL: atomico no-replace (macOS 10.12+).
        result = ::renamex_np(tempPath.c_str(), finalPath.c_str(), RENAME_EXCL);
        savedErrno = errno;
#endif
        if (result == 0) {
            return PublishStatus::Success;
        }
        if (savedErrno == EEXIST) {
            return PublishStatus::Exists;
        }
        // Fallback SOLO se la primitiva non è supportata dal filesystem.
        // Altri errori (es. EACCES) sono condizioni reali: fail-safe immediato
        // senza cadere in un messaggio fuorviante.
        if (savedErrno == EINVAL || savedErrno == ENOSYS || savedErrno == EOPNOTSUPP) {
            return publishNoReplaceFallback(tempPath, finalPath);
        }
        return PublishStatus::NoAtomicSupport;
#elif defined(_WIN32)
        // Senza MOVEFILE_REPLACE_EXISTING fallisce se la destinazione esiste.
        if (::MoveFileExA(tempPath.c_str(), finalPath.c_str(), MOVEFILE_WRITE_THROUGH) != 0) {
            return PublishStatus::Success;
        }
        if (::GetLastError() == ERROR_ALREADY_EXISTS) {
            return PublishStatus::Exists;
        }
        return PublishStatus::NoAtomicSupport;
#else
        // Piattaforma sconosciuta: niente primitiva no-replace → fail-closed,
        // mai un fallback che possa sovrascrivere.
        (void)tempPath;
        (void)finalPath;
        return PublishStatus::NoAtomicSupport;
#endif
    }

} // namespace Core
