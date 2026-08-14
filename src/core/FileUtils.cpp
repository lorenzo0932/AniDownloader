#include "core/FileUtils.hpp"

#include <filesystem>
#include <fstream>
#include <regex>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <chrono>

namespace Core {

int countVideoFiles(const std::string& dirPath) {
    int count = 0;
    std::filesystem::path dir(dirPath);
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
        return 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".mp4" || ext == ".mkv" || ext == ".webm" || ext == ".avi" || ext == ".mov")
            count++;
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
        if (!home) { dp = std::filesystem::path(dirPath); }
        else {
            dp = dirPath == "~"
                ? std::filesystem::path(home)
                : std::filesystem::path(home) / dirPath.substr(2);
        }
    } else {
        dp = std::filesystem::path(dirPath);
    }

    if (!std::filesystem::exists(dp) || !std::filesystem::is_directory(dp))
        return entries;

    for (const auto& entry : std::filesystem::directory_iterator(dp)) {
        if (!entry.is_directory()) continue;
        auto filename = entry.path().filename().string();
        if (filename[0] == '.') continue;
        auto ftime = std::filesystem::last_write_time(entry);
        // Conversione portatile file_clock -> system_clock: l'epoch di file_time_type
        // dipende dalla piattaforma (libstdc++ usa 2174-01-01 -> mtime negativi).
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        auto mtime = std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
        entries.push_back({filename, entry.path().string(), mtime});
    }

    std::sort(entries.begin(), entries.end(),
        [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });

    return entries;
}

} // namespace Core
