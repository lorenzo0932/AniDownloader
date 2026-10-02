#include "core/ThumbCache.hpp"

#include "config/PathHelper.hpp"
#include "core/FileUtils.hpp"
#include "core/ProcessUtils.hpp"
#include "scrapers/ScraperUtils.hpp"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

namespace Core {
    namespace fs = std::filesystem;

    namespace {
        // FNV-1a 64bit: deterministica per sempre (niente dipendenze,
        // a differenza di std::hash), collisioni trascurabili a questa scala.
        uint64_t fnv1a(const std::string& s) {
            uint64_t h = 14695981039346656037ULL;
            for (unsigned char c : s) {
                h ^= c;
                h *= 1099511628211ULL;
            }
            return h;
        }

        std::string toHex(uint64_t v) {
            std::ostringstream out;
            out << std::hex << v;
            return out.str();
        }

        fs::path thumbCacheDir() { return Config::PathHelper::getThumbCacheDir(); }

        uint64_t fileMtimeSecs(const fs::path& p, std::error_code& ec) {
            auto t = fs::last_write_time(p, ec);
            if (ec)
                return 0;
            auto secs = std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch());
            return static_cast<uint64_t>(secs.count());
        }
    } // namespace

    int selectThumbWidth(int requested) {
        if (requested <= 0)
            return THUMB_DEFAULT_WIDTH;
        for (int t : THUMB_WIDTHS)
            if (requested <= t)
                return t;
        return THUMB_WIDTHS.back();
    }

    int clampThumbQuality(int requested) {
        if (requested < 1 || requested > 100)
            return THUMB_DEFAULT_QUALITY;
        return requested;
    }

    std::string thumbCacheKey(const std::string& absPath, uint64_t mtime, uint64_t size, int w,
                              int q) {
        std::ostringstream raw;
        raw << absPath << '\x1f' << mtime << '\x1f' << size << '\x1f' << w << 'x' << q;
        return toHex(fnv1a(raw.str()));
    }

    std::optional<std::string> thumbFor(const std::string& seriesPath, int w, int q) {
        const std::string poster = findPosterPath(seriesPath);
        if (poster.empty())
            return std::nullopt;
        std::error_code ec;
        const uint64_t size = fs::file_size(poster, ec);
        if (ec || size == 0)
            return std::nullopt;
        const uint64_t mtime = fileMtimeSecs(poster, ec);
        if (ec)
            return std::nullopt;

        const fs::path dir = thumbCacheDir();
        fs::create_directories(dir, ec);
        if (ec)
            return std::nullopt;
        const fs::path final = dir / (thumbCacheKey(poster, mtime, size, w, q) + ".webp");
        if (fs::exists(final, ec) && !ec && fs::file_size(final, ec) > 0 && !ec)
            return final.string();

        // Mai upscale (come il c_limit dei CDN): se il sorgente e' piu'
        // piccolo del tier, resta a dimensione naturale.
        std::ostringstream filter;
        filter << "scale=min(iw\\," << w << "):-2:flags=lanczos";
        // Scrittura atomica via rename; il tmp tiene l'estensione .webp
        // perche' ffmpeg deduce il muxer dall'estensione.
        const fs::path tmp = dir / (final.filename().string() + ".tmp.webp");
        std::ostringstream cmd;
        cmd << "ffmpeg -v error -y -i " << ScraperUtils::Q(poster) << " -vf "
            << ScraperUtils::Q(filter.str()) << " -frames:v 1 -c:v libwebp -q:v " << q << " -an "
            << ScraperUtils::Q(tmp.string());
        static std::atomic<bool> dummyStop{false};
        if (ProcessUtils::runCommand(cmd.str(), dummyStop, nullptr) != 0) {
            fs::remove(tmp, ec);
            return std::nullopt;
        }
        if (!fs::exists(tmp, ec) || ec || fs::file_size(tmp, ec) == 0 || ec) {
            fs::remove(tmp, ec);
            return std::nullopt;
        }
        fs::rename(tmp, final, ec);
        if (ec)
            return std::nullopt;
        // Tetto LRU: il file nuovo e' il piu' recente, mai evitto qui.
        (void)enforceThumbCacheCap(dir.string(), THUMB_CACHE_MAX_BYTES);
        return final.string();
    }

    ThumbCacheInfo thumbCacheInfo() {
        ThumbCacheInfo info;
        std::error_code ec;
        if (!fs::exists(thumbCacheDir(), ec) || ec)
            return info;
        for (const auto& e : fs::directory_iterator(thumbCacheDir(), ec)) {
            if (ec)
                break;
            if (!e.is_regular_file(ec) || ec)
                continue;
            const uint64_t s = e.file_size(ec);
            if (ec)
                continue;
            ++info.files;
            info.bytes += s;
        }
        return info;
    }

    ThumbCacheInfo clearThumbCache() {
        ThumbCacheInfo removed;
        std::error_code ec;
        if (!fs::exists(thumbCacheDir(), ec) || ec)
            return removed;
        for (const auto& e : fs::directory_iterator(thumbCacheDir(), ec)) {
            if (ec)
                break;
            if (!e.is_regular_file(ec) || ec)
                continue;
            const uint64_t s = e.file_size(ec);
            if (ec)
                continue;
            if (fs::remove(e.path(), ec) && !ec) {
                ++removed.files;
                removed.bytes += s;
            }
        }
        return removed;
    }

    ThumbCacheInfo enforceThumbCacheCap(const std::string& dir, uint64_t maxBytes) {
        struct Entry {
            uint64_t mtime;
            uint64_t size;
            fs::path path;
        };
        std::vector<Entry> entries;
        uint64_t total = 0;
        std::error_code ec;
        if (!fs::exists(dir, ec) || ec)
            return {};
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (ec)
                break;
            if (!e.is_regular_file(ec) || ec)
                continue;
            const uint64_t s = e.file_size(ec);
            if (ec)
                continue;
            entries.push_back({fileMtimeSecs(e.path(), ec), s, e.path()});
            total += s;
        }
        std::sort(entries.begin(), entries.end(),
                  [](const Entry& a, const Entry& b) { return a.mtime < b.mtime; });
        ThumbCacheInfo removed;
        for (const auto& e : entries) {
            if (total <= maxBytes)
                break;
            if (fs::remove(e.path, ec) && !ec) {
                ++removed.files;
                removed.bytes += e.size;
                total -= e.size;
            }
        }
        return removed;
    }
} // namespace Core
