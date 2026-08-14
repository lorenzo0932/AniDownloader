#pragma once

#include <string>
#include <vector>

namespace Core {

    int countVideoFiles(const std::string& dirPath);
    std::string readNfoDescription(const std::string& seriesPath);
    std::string findPosterPath(const std::string& seriesPath);

    struct DirEntry {
        std::string name;
        std::string path;
        int64_t mtime = 0;
    };

    std::vector<DirEntry> listDirectories(const std::string& dirPath);

} // namespace Core
