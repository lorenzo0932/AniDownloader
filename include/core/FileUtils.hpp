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

    // Publish atomico no-replace (feature 11): pubblica il file temporaneo
    // con il nome finale solo se la destinazione NON esiste. Primitiva nativa
    // per piattaforma: renameat2(RENAME_NOREPLACE) su Linux,
    // renamex_np(RENAME_EXCL) su macOS, MoveFileExW senza
    // MOVEFILE_REPLACE_EXISTING su Windows. Fail-closed: se la primitiva non
    // è disponibile (es. ENOSYS su kernel vecchi) ritorna false, mai un
    // fallback non atomico. Su EEXIST lascia il temporaneo al suo posto.
    bool publishNoReplace(const std::string& tempPath, const std::string& finalPath);

} // namespace Core
