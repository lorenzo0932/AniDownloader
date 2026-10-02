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
#include <sstream>
#ifdef __linux__
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// _open/_close per la creazione esclusiva (O_EXCL) di createEmptyFile.
#include <io.h>
#include <sys/stat.h>
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
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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

    std::string expandUserPath(const std::string& dirPath) {
        if (dirPath == "~" || dirPath.rfind("~/", 0) == 0) {
            const char* home = std::getenv("HOME");
#ifdef _WIN32
            // Su Windows HOME spesso non esiste: fallback su %USERPROFILE%.
            if (!home)
                home = std::getenv("USERPROFILE");
#endif
            if (!home)
                return dirPath;
            return dirPath == "~" ? std::string(home) : std::string(home) + dirPath.substr(1);
        }
        return dirPath;
    }

    std::vector<DirEntry> listDirectories(const std::string& dirPath, bool includeFiles) {
        std::vector<DirEntry> entries;
        std::filesystem::path dp(expandUserPath(dirPath));

        std::error_code ec;
        if (!std::filesystem::exists(dp, ec) || !std::filesystem::is_directory(dp, ec))
            return entries;

        for (const auto& entry : std::filesystem::directory_iterator(dp, ec)) {
            if (ec)
                break;
            std::error_code fileEc;
            bool isDir = entry.is_directory(fileEc);
            if (fileEc)
                continue;
            if (!isDir && !includeFiles)
                continue;
            if (!isDir && !entry.is_regular_file(fileEc))
                continue;
            if (fileEc)
                continue;
            auto filename = entry.path().filename().string();
            if (filename.empty() || filename[0] == '.')
                continue;
            auto ftime = std::filesystem::last_write_time(entry);
            // Conversione portatile file_clock -> system_clock: l'epoch di file_time_type
            // dipende dalla piattaforma (libstdc++ usa 2174-01-01 -> mtime negativi).
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() +
                std::chrono::system_clock::now());
            auto mtime =
                std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
            DirEntry de;
            de.name = filename;
            de.path = entry.path().string();
            de.mtime = mtime;
            de.isDir = isDir;
            if (!isDir) {
                std::error_code sizeEc;
                auto sz = std::filesystem::file_size(entry.path(), sizeEc);
                de.size = sizeEc ? 0 : static_cast<int64_t>(sz);
            }
            entries.push_back(de);
        }

        std::sort(entries.begin(), entries.end(), [](const DirEntry& a, const DirEntry& b) {
            if (a.isDir != b.isDir)
                return a.isDir; // directory prima dei file
            return a.name < b.name;
        });

        return entries;
    }

    // ---- Mount points ----
    namespace {
        // Pseudo-filesystem da escludere: non sono dischi navigabili dall'utente.
        // squashfs/nsfs in lista: gli snap (squashfs) e i namespace non sono
        // volumi che l'utente voglia sfogliare nel picker.
        bool isPseudoFs(const std::string& fsType) {
            static const char* pseudo[] = {
                "proc",       "sysfs",     "devtmpfs",    "devpts",  "cgroup",     "cgroup2",
                "securityfs", "pstore",    "debugfs",     "tracefs", "configfs",   "fusectl",
                "mqueue",     "hugetlbfs", "binfmt_misc", "autofs",  "rpc_pipefs", "nsfs",
                "bpf",        "selinuxfs", "efivarfs",    "squashfs"};
            for (const char* p : pseudo)
                if (fsType == p)
                    return true;
            return false;
        }

        // Anche tmpfs/overlay vanno esclusi come mount "utili": tmpfs e' memoria
        // volatile, overlay e' il filesystem della macchina container.
        bool isVolatileOrOverlay(const std::string& fsType) {
            return fsType == "tmpfs" || fsType == "overlay";
        }

        bool isOctalDigit(char c) { return c >= '0' && c <= '7'; }

        bool underPrefix(const std::string& p, const std::string& prefix) {
            return p == prefix || p.rfind(prefix + "/", 0) == 0;
        }

        // Path di sistema: mai offerti come "dischi" nel picker, anche se il
        // fstype e' reale (es. /boot in ext4, snap in squashfs sotto
        // /var/lib/snapd). /run fa eccezione per /run/media (chiavette
        // montate dall'utente). /home esatta e' coperta dalla voce "Home"
        // della sidebar: come volume sarebbe solo rumore.
        bool isSystemMountPath(const std::string& mp) {
            if (underPrefix(mp, "/run") && !underPrefix(mp, "/run/media"))
                return true;
            static const char* sys[] = {"/proc",
                                        "/sys",
                                        "/dev",
                                        "/snap",
                                        "/boot",
                                        "/var/lib/docker",
                                        "/var/lib/containers",
                                        "/var/lib/snapd",
                                        "/var/snap"};
            for (const char* prefix : sys)
                if (underPrefix(mp, prefix))
                    return true;
            return mp == "/home";
        }

        // Decodifica le sequenze octal di /proc/self/mounts (\040 = spazio).
        // Solo sequenze valide (backslash + 3 cifre octal, prima 0-3):
        // escape troncati o non octal passano invariati, niente garbage.
        std::string decodeMountEscapes(const std::string& s) {
            std::string out;
            for (size_t i = 0; i < s.size();) {
                if (s[i] == '\\' && i + 3 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '3' &&
                    isOctalDigit(s[i + 2]) && isOctalDigit(s[i + 3])) {
                    int v = (s[i + 1] - '0') * 64 + (s[i + 2] - '0') * 8 + (s[i + 3] - '0');
                    out += static_cast<char>(v);
                    i += 4;
                } else {
                    out += s[i++];
                }
            }
            return out;
        }
    } // namespace

    std::vector<MountEntry> parseMountTable(const std::string& content) {
        std::vector<MountEntry> mounts;
        std::istringstream in(content);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty())
                continue;
            // Formato: device mountpoint fstype options dump pass
            std::istringstream ls(line);
            std::string device, mountpoint, fstype;
            if (!(ls >> device >> mountpoint >> fstype))
                continue;
            if (isPseudoFs(fstype) || isVolatileOrOverlay(fstype))
                continue;
            // Path di sistema (snap, boot, docker, /run non-media, ...):
            // volumi reali ma non navigabili dall'utente, fuori dalla sidebar.
            if (isSystemMountPath(mountpoint))
                continue;
            std::string decoded = decodeMountEscapes(mountpoint);
            // Etichetta = basename ("ipool00002", non "/mnt/ipool00002"):
            // il path completo resta comunque nel campo path.
            std::string label = decoded;
            if (decoded != "/") {
                std::string base = std::filesystem::path(decoded).filename().string();
                if (!base.empty())
                    label = base;
            }
            // Bind mount: stesso path da device diversi -> una sola voce.
            bool dup = false;
            for (const auto& m : mounts)
                if (m.path == decoded) {
                    dup = true;
                    break;
                }
            if (!dup)
                mounts.push_back({label, decoded});
        }
        return mounts;
    }

    std::vector<std::string> parseAllMountPoints(const std::string& content) {
        std::vector<std::string> points;
        std::istringstream in(content);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty())
                continue;
            std::istringstream ls(line);
            std::string device, mountpoint, fstype;
            if (!(ls >> device >> mountpoint >> fstype))
                continue;
            std::string decoded = decodeMountEscapes(mountpoint);
            if (std::find(points.begin(), points.end(), decoded) == points.end())
                points.push_back(decoded);
        }
        return points;
    }

    std::vector<std::string> listAllMountPoints() {
        std::vector<std::string> points;
#if defined(_WIN32)
        // Tutte le lettere presenti, anche di tipo UNKNOWN: una radice di
        // drive esistente e' comunque un punto di mount da proteggere.
        DWORD mask = ::GetLogicalDrives();
        for (int i = 0; i < 26; ++i) {
            if (mask & (1u << i))
                points.push_back(std::string(1, static_cast<char>('A' + i)) + ":\\");
        }
#elif defined(__APPLE__)
        points.push_back("/");
        std::error_code ec;
        std::filesystem::directory_iterator it("/Volumes", ec);
        if (!ec) {
            for (const auto& entry : it) {
                std::error_code dirEc;
                if (std::filesystem::is_directory(entry.path(), dirEc))
                    points.push_back(entry.path().string());
            }
        }
#else
        std::ifstream mountsFile("/proc/self/mounts");
        if (mountsFile) {
            std::string content((std::istreambuf_iterator<char>(mountsFile)), {});
            points = parseAllMountPoints(content);
        }
        if (std::find(points.begin(), points.end(), "/") == points.end())
            points.insert(points.begin(), "/");
#endif
        return points;
    }

    // ---- Posizioni principali (sidebar del picker) ----
    std::vector<PlaceEntry> parseUserDirsFile(const std::string& content, const std::string& home) {
        struct KeyMap {
            const char* key;
            const char* id;
        };
        static const KeyMap keys[] = {
            {"XDG_DESKTOP_DIR", "desktop"},    {"XDG_DOCUMENTS_DIR", "documents"},
            {"XDG_DOWNLOAD_DIR", "downloads"}, {"XDG_MUSIC_DIR", "music"},
            {"XDG_PICTURES_DIR", "pictures"},  {"XDG_VIDEOS_DIR", "videos"}};
        std::vector<PlaceEntry> out;
        std::istringstream in(content);
        std::string line;
        while (std::getline(in, line)) {
            size_t s = line.find_first_not_of(" \t");
            if (s == std::string::npos || line[s] == '#')
                continue;
            for (const auto& k : keys) {
                std::string key = k.key;
                if (line.compare(s, key.size(), key) != 0)
                    continue;
                size_t eq = line.find('=', s + key.size());
                if (eq == std::string::npos)
                    continue;
                std::string val = line.substr(eq + 1);
                // trim spazi e virgolette
                size_t a = val.find_first_not_of(" \t\"'");
                if (a == std::string::npos)
                    continue;
                size_t b = val.find_last_not_of(" \t\"'");
                val = val.substr(a, b - a + 1);
                if (val.rfind("$HOME/", 0) == 0)
                    val = home + val.substr(5);
                else if (val == "$HOME")
                    val = home;
                else if (!val.empty() && val[0] != '/')
                    val = home + "/" + val; // relativo: da spec e' sotto $HOME
                if (val.empty() || val[0] != '/')
                    continue;
                // Directory DISABILITATA: da spec xdg-user-dirs, puntare alla
                // home stessa ($HOME o $HOME/) significa "non usare questa
                // voce" (es. XDG_DESKTOP_DIR="$HOME/"). Senza questo check il
                // picker mostrerebbe una "Scrivania" uguale alla home.
                std::string noSlash = val;
                while (noSlash.size() > 1 && noSlash.back() == '/')
                    noSlash.pop_back();
                std::string homeNoSlash = home;
                while (homeNoSlash.size() > 1 && homeNoSlash.back() == '/')
                    homeNoSlash.pop_back();
                if (!homeNoSlash.empty() && noSlash == homeNoSlash)
                    continue;
                std::string name = std::filesystem::path(val).filename().string();
                if (name.empty())
                    name = val;
                out.push_back({k.id, name, val});
            }
        }
        return out;
    }

    std::vector<PlaceEntry> listPlaces() {
        std::vector<PlaceEntry> places;
        const char* homeEnv = std::getenv("HOME");
#ifdef _WIN32
        if (!homeEnv)
            homeEnv = std::getenv("USERPROFILE");
#endif
        std::string home = homeEnv ? homeEnv : "";
        auto addIfDir = [&](const std::string& id, const std::string& name, const std::string& p) {
            if (p.empty())
                return;
            std::error_code ec;
            if (!std::filesystem::is_directory(p, ec))
                return;
            for (const auto& pl : places)
                if (pl.path == p)
                    return;
            places.push_back({id, name, p});
        };
        if (!home.empty())
            addIfDir("home", "Home", home);
#if defined(_WIN32)
        const std::string sep = "\\";
        addIfDir("desktop", "Desktop", home + sep + "Desktop");
        addIfDir("documents", "Documents", home + sep + "Documents");
        addIfDir("downloads", "Downloads", home + sep + "Downloads");
        addIfDir("music", "Music", home + sep + "Music");
        addIfDir("pictures", "Pictures", home + sep + "Pictures");
        addIfDir("videos", "Videos", home + sep + "Videos");
#elif defined(__APPLE__)
        addIfDir("desktop", "Desktop", home + "/Desktop");
        addIfDir("documents", "Documents", home + "/Documents");
        addIfDir("downloads", "Downloads", home + "/Downloads");
        addIfDir("movies", "Movies", home + "/Movies");
        addIfDir("music", "Music", home + "/Music");
        addIfDir("pictures", "Pictures", home + "/Pictures");
#else
        std::vector<PlaceEntry> xdg;
        std::string cfgFile;
        if (const char* xdgHome = std::getenv("XDG_CONFIG_HOME"))
            cfgFile = std::string(xdgHome) + "/user-dirs.dirs";
        else if (!home.empty())
            cfgFile = home + "/.config/user-dirs.dirs";
        if (!cfgFile.empty()) {
            std::ifstream f(cfgFile);
            if (f) {
                std::string content((std::istreambuf_iterator<char>(f)), {});
                xdg = parseUserDirsFile(content, home);
            }
        }
        for (const auto& pl : xdg)
            addIfDir(pl.id, pl.name, pl.path);
        // Fallback se user-dirs.dirs manca: candidati convenzionali EN/IT.
        // Solo quelli esistenti, quindi innocui su qualunque distro/lingua.
        if (xdg.empty() && !home.empty()) {
            addIfDir("desktop", "Desktop", home + "/Desktop");
            addIfDir("desktop", "Scrivania", home + "/Scrivania");
            addIfDir("documents", "Documents", home + "/Documents");
            addIfDir("documents", "Documenti", home + "/Documenti");
            addIfDir("downloads", "Downloads", home + "/Downloads");
            addIfDir("downloads", "Scaricati", home + "/Scaricati");
            addIfDir("music", "Music", home + "/Music");
            addIfDir("music", "Musica", home + "/Musica");
            addIfDir("pictures", "Pictures", home + "/Pictures");
            addIfDir("pictures", "Immagini", home + "/Immagini");
            addIfDir("videos", "Videos", home + "/Videos");
            addIfDir("videos", "Video", home + "/Video");
        }
#endif
        return places;
    }

    bool isWindowsDriveRoot(const std::string& path) {
        if (path.size() < 2 || path.size() > 3)
            return false;
        if (!std::isalpha(static_cast<unsigned char>(path[0])) || path[1] != ':')
            return false;
        return path.size() == 2 || path[2] == '\\' || path[2] == '/';
    }

    std::string browseParentPath(const std::string& normalizedPath) {
        if (normalizedPath.empty())
            return {};
        std::filesystem::path p(normalizedPath);
        // Root ("/" su POSIX, "C:\" su Windows): nessun parent.
        if (p.has_root_path() && p.relative_path().empty())
            return {};
        std::filesystem::path up = p.parent_path();
        if (up.empty() || up == p)
            return {};
        return up.string();
    }

    std::vector<MountEntry> listMounts() {
        std::vector<MountEntry> mounts;

#if defined(_WIN32)
        DWORD mask = ::GetLogicalDrives();
        for (int i = 0; i < 26; ++i) {
            if (!(mask & (1u << i)))
                continue;
            std::string root = std::string(1, static_cast<char>('A' + i)) + ":\\";
            UINT type = ::GetDriveTypeA(root.c_str());
            std::string label;
            switch (type) {
            case DRIVE_FIXED:
                label = "Disco locale (" + std::string(1, static_cast<char>('A' + i)) + ":)";
                break;
            case DRIVE_REMOVABLE:
                label = "Rimovibile (" + std::string(1, static_cast<char>('A' + i)) + ":)";
                break;
            case DRIVE_REMOTE:
                label = "Rete (" + std::string(1, static_cast<char>('A' + i)) + ":)";
                break;
            case DRIVE_CDROM:
                label = "CD/DVD (" + std::string(1, static_cast<char>('A' + i)) + ":)";
                break;
            default:
                continue; // DRIVE_NO_ROOT_DIR, DRIVE_UNKNOWN: navigabile solo se
                          // esiste, ma non e' un volume utile da offrire
            }
            mounts.push_back({label, root});
        }
        if (mounts.empty())
            mounts.push_back({"File system", "/"});
        return mounts;
#elif defined(__APPLE__)
        std::error_code ec;
        std::filesystem::directory_iterator it("/Volumes", ec);
        if (!ec) {
            for (const auto& entry : it) {
                // is_directory segue i symlink: alcune voci di /Volumes lo sono
                // (dischi montati via link), e vanno comunque offerte.
                std::error_code dirEc;
                if (!std::filesystem::is_directory(entry.path(), dirEc))
                    continue;
                std::string name = entry.path().filename().string();
                if (name.empty() || name[0] == '.')
                    continue;
                mounts.push_back({name, entry.path().string()});
            }
        }
        if (mounts.empty())
            mounts.push_back({"Macintosh HD", "/"});
        else
            // La root resta in cima come su Linux: serve per uscire da un
            // mount nidificato senza doverla cercare in fondo.
            mounts.insert(mounts.begin(), {"/", "/"});
        return mounts;
#else
        std::ifstream mountsFile("/proc/self/mounts");
        if (mountsFile) {
            std::string content((std::istreambuf_iterator<char>(mountsFile)), {});
            mounts = parseMountTable(content);
        }
        // La root e' sempre disponibile: senza di lei il picker non potrebbe
        // uscire da un mount nidificato.
        bool hasRoot = false;
        for (auto& m : mounts)
            if (m.path == "/")
                hasRoot = true;
        if (!hasRoot)
            mounts.insert(mounts.begin(), {"/", "/"});
        return mounts;
#endif
    }

    // ---- Operazioni filesystem (file picker) ----
    namespace {
        constexpr size_t MAX_NAME_LEN = 255;
        constexpr uint64_t COUNT_LIMIT = 10000;

        FsOpStatus classifyError(const std::error_code& ec) {
            if (ec == std::errc::permission_denied)
                return FsOpStatus::NotPermitted;
            if (ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory)
                return FsOpStatus::NotFound;
            if (ec == std::errc::file_exists)
                return FsOpStatus::Exists;
            return FsOpStatus::IoError;
        }

        // Un nome singolo: niente separatori, niente "." / "..", niente NUL.
        bool isValidEntryName(const std::string& name) {
            if (name.empty() || name.size() > MAX_NAME_LEN)
                return false;
            if (name == "." || name == "..")
                return false;
            if (name.find('\0') != std::string::npos)
                return false;
            if (name.find('/') != std::string::npos)
                return false;
            if (name.find('\\') != std::string::npos)
                return false;
            return true;
        }

        // Su Windows l'attributo read-only blocca sia remove() sia remove_all():
        // va azzerato prima. Esiste solo su _WIN32 (su POSIX i permessi bastano).
#ifdef _WIN32
        void clearReadOnlyEntry(const std::filesystem::path& p) {
            auto attrs = ::GetFileAttributesA(p.string().c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY))
                ::SetFileAttributesA(p.string().c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
        }
#endif

        void clearReadOnlyRecursive(const std::filesystem::path& p) {
#ifdef _WIN32
            std::error_code ec;
            if (std::filesystem::is_directory(p, ec)) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(p, ec)) {
                    if (ec)
                        break;
                    clearReadOnlyEntry(entry.path());
                }
            } else {
                clearReadOnlyEntry(p);
            }
#else
            (void)p;
#endif
        }

        // Conta gli elementi (non vuota = >0). Tetto a COUNT_LIMIT, con uscita
        // anticipata: su alberi enormi non serve camminare tutto, il 409 dice
        // comunque "10k+ elementi".
        bool dirHasChildren(const std::filesystem::path& p, uint64_t* outCount) {
            std::error_code ec;
            uint64_t count = 0;
            std::filesystem::recursive_directory_iterator it(p, ec);
            if (ec)
                return false;
            for (const auto& entry : it) {
                (void)entry;
                if (ec)
                    break;
                if (count >= COUNT_LIMIT)
                    break;
                ++count;
            }
            if (outCount)
                *outCount = count;
            return count > 0;
        }
    } // namespace

    namespace {
        // Crea un file vuoto in modo esclusivo: fallisce (senza toccare
        // nulla) se il path esiste gia'. Equivalente cross-platform di
        // open(O_CREAT | O_EXCL).
        bool createExclusiveEmpty(const std::filesystem::path& target, std::error_code& ec) {
#ifdef _WIN32
            int fd = ::_open(target.string().c_str(), _O_CREAT | _O_EXCL | _O_WRONLY,
                             _S_IREAD | _S_IWRITE);
            if (fd == -1) {
                ec.assign(errno, std::generic_category());
                return false;
            }
            ::_close(fd);
#else
            int fd = ::open(target.string().c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
            if (fd == -1) {
                ec.assign(errno, std::generic_category());
                return false;
            }
            ::close(fd);
#endif
            return true;
        }
    } // namespace

    FsOpStatus createDirectory(const std::string& parent, const std::string& name,
                               std::string* outPath) {
        if (!isValidEntryName(name))
            return FsOpStatus::InvalidName;
        std::filesystem::path base(expandUserPath(parent));
        std::error_code ec;
        if (!std::filesystem::is_directory(base, ec))
            return FsOpStatus::NotFound;
        std::filesystem::path target = base / name;
        if (std::filesystem::exists(target, ec))
            return FsOpStatus::Exists;
        if (!std::filesystem::create_directory(target, ec)) {
            if (ec)
                return classifyError(ec);
            return FsOpStatus::Exists;
        }
        if (outPath)
            *outPath = target.string();
        return FsOpStatus::Ok;
    }

    FsOpStatus createEmptyFile(const std::string& parent, const std::string& name,
                               std::string* outPath) {
        if (!isValidEntryName(name))
            return FsOpStatus::InvalidName;
        std::filesystem::path base(expandUserPath(parent));
        std::error_code ec;
        if (!std::filesystem::is_directory(base, ec))
            return FsOpStatus::NotFound;
        std::filesystem::path target = base / name;
        if (std::filesystem::exists(target, ec))
            return FsOpStatus::Exists;
        // Creazione esclusiva (O_EXCL): se un altro processo crea il file
        // nel frattempo, fallisce con EEXIST invece di "riuscire" su un file
        // preesistente. Niente truncate, niente clobber.
        std::error_code crEc;
        if (!createExclusiveEmpty(target, crEc)) {
            if (crEc == std::errc::file_exists)
                return FsOpStatus::Exists;
            return classifyError(crEc);
        }
        if (outPath)
            *outPath = target.string();
        return FsOpStatus::Ok;
    }

    FsOpStatus removePath(const std::string& path, bool recursive, uint64_t* outCount) {
        if (outCount)
            *outCount = 0;
        if (path.empty())
            return FsOpStatus::InvalidName;
        std::filesystem::path target(expandUserPath(path));

        // --- Guardrail: mai rimuovere la root o un punto di mount ---
        // PRIMA del check di esistenza: un mount point resta protetto anche
        // se stat fallisce (permessi, namespace altrui) o la voce e' stantia.
        // In tutti questi casi la risposta e' comunque un rifiuto.
        std::string normalized = target.lexically_normal().string();
        if (normalized == "/" || normalized == "\\" || normalized == "." || normalized.empty())
            return FsOpStatus::NotPermitted;
#ifdef _WIN32
        // Radice di un drive ("C:", "C:\", "C:/"): vale per qualunque
        // lettera, anche se GetDriveTypeA la riporta UNKNOWN.
        if (isWindowsDriveRoot(normalized))
            return FsOpStatus::NotPermitted;
#endif
        // TUTTI i punti di mount, non solo quelli mostrati nel picker:
        // listAllMountPoints() include anche pseudo-fs e tmpfs/overlay.
        for (const auto& mp : listAllMountPoints()) {
            if (normalized == std::filesystem::path(mp).lexically_normal().string())
                return FsOpStatus::NotPermitted;
        }

        std::error_code ec;
        if (!std::filesystem::exists(target, ec))
            return FsOpStatus::NotFound;

        // symlink_status NON segue il link: un symlink (anche a directory)
        // si rimuove come file. Senza questo, il pre-walk qui sotto
        // (conteggio, clear read-only) attraverserebbe il bersaglio e
        // toccherebbe dati esterni al link.
        std::error_code linkEc;
        bool isLink = std::filesystem::is_symlink(std::filesystem::symlink_status(target, linkEc));
        if (linkEc)
            return FsOpStatus::IoError;

        bool isDir = false;
        if (!isLink) {
            isDir = std::filesystem::is_directory(target, ec);
            if (ec)
                return FsOpStatus::IoError;
        }

        if (isDir) {
            uint64_t count = 0;
            bool nonEmpty = dirHasChildren(target, &count);
            if (outCount)
                *outCount = count;
            if (nonEmpty && !recursive)
                return FsOpStatus::NotEmpty;
        }

        if (!isLink)
            clearReadOnlyRecursive(target);
        // remove/remove_all non seguono i symlink: sul link eliminano il
        // link, non il bersaglio.
        std::error_code rmEc;
        if (isDir) {
            std::filesystem::remove_all(target, rmEc);
        } else {
            std::filesystem::remove(target, rmEc);
        }
        if (rmEc)
            return classifyError(rmEc);
        return FsOpStatus::Ok;
    }

#if defined(__linux__) || defined(__APPLE__)
    // Fallback atomico no-replace con link()+unlink(): il file finale diventa
    // un hard link al temporaneo (stesso inode) e il temporaneo viene rimosso.
    // link() fallisce con EEXIST se la destinazione esiste: mai sovrascrittura.
    PublishStatus publishNoReplaceFallback(const std::string& tempPath,
                                           const std::string& finalPath, int* outErrno) {
        if (::link(tempPath.c_str(), finalPath.c_str()) != 0) {
            int err = errno;
            if (outErrno)
                *outErrno = err;
            if (err == EEXIST) {
                return PublishStatus::Exists;
            }
            // EPERM/ENOSYS/EOPNOTSUPP/EMLINK = il filesystem non supporta gli
            // hard link (FAT/exFAT/9p): mancanza di feature, non un errore.
            if (err == EPERM || err == ENOSYS || err == EOPNOTSUPP || err == EMLINK) {
                return PublishStatus::NoAtomicSupport;
            }
            // Qualsiasi altro errno (ENOENT, EACCES, EXDEV...) è una
            // condizione reale: non va spacciata per "atomicità non supportata".
            return PublishStatus::Error;
        }
        // Il file finale (stesso inode) esiste già: la rimozione del temporaneo
        // è fattibile; un eventuale errore qui non compromette il risultato.
        ::unlink(tempPath.c_str());
        return PublishStatus::Success;
    }
#endif

    PublishStatus publishNoReplace(const std::string& tempPath, const std::string& finalPath,
                                   int* outErrno) {
#if defined(__linux__) || defined(__APPLE__)
        long result = -1;
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
            if (outErrno)
                *outErrno = 0;
            return PublishStatus::Success;
        }
        if (outErrno)
            *outErrno = savedErrno;
        if (savedErrno == EEXIST) {
            return PublishStatus::Exists;
        }
        // Fallback SOLO se la primitiva non è supportata dal filesystem
        // (flag RENAME_NOREPLACE/NOREPLACE non implementato dal fs).
        if (savedErrno == EINVAL || savedErrno == ENOSYS || savedErrno == EOPNOTSUPP) {
            return publishNoReplaceFallback(tempPath, finalPath, outErrno);
        }
        // ENOENT/EACCES/ENOSPC/EXDEV/EBUSY... sono condizioni reali: fail-safe
        // immediato, Error → il chiamante riporta l'errno invece di un
        // fuorviante "il filesystem non supporta l'atomicità".
        return PublishStatus::Error;
#elif defined(_WIN32)
        // Senza MOVEFILE_REPLACE_EXISTING fallisce se la destinazione esiste.
        if (::MoveFileExA(tempPath.c_str(), finalPath.c_str(), MOVEFILE_WRITE_THROUGH) != 0) {
            if (outErrno)
                *outErrno = 0;
            return PublishStatus::Success;
        }
        DWORD err = ::GetLastError();
        if (outErrno)
            *outErrno = static_cast<int>(err);
        if (err == ERROR_ALREADY_EXISTS) {
            return PublishStatus::Exists;
        }
        // ERROR_NOT_SUPPORTED = il fs non espone il no-replace nativo.
        if (err == ERROR_NOT_SUPPORTED) {
            return PublishStatus::NoAtomicSupport;
        }
        return PublishStatus::Error;
#else
        // Piattaforma sconosciuta: niente primitiva no-replace → fail-closed,
        // mai un fallback che possa sovrascrivere.
        (void)tempPath;
        (void)finalPath;
        if (outErrno)
            *outErrno = 0;
        return PublishStatus::NoAtomicSupport;
#endif
    }

} // namespace Core
