#include "core/FileUtils.hpp"

#include <filesystem>
#include <fstream>
#include <regex>
#include <algorithm>
#include <cctype>

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

std::vector<std::pair<std::string, std::string>> listDirectories(const std::string& dirPath) {
    std::vector<std::pair<std::string, std::string>> entries;
    std::filesystem::path dp(dirPath);
    if (!std::filesystem::exists(dp) || !std::filesystem::is_directory(dp))
        return entries;
    for (const auto& entry : std::filesystem::directory_iterator(dp)) {
        if (entry.is_directory()) {
            entries.emplace_back(entry.path().filename().string(), entry.path().string());
        }
    }
    return entries;
}

} // namespace Core
