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

    enum class PublishStatus { Success, Exists, NoAtomicSupport };

    // Publish atomico no-replace (feature 11): pubblica il file temporaneo
    // con il nome finale solo se la destinazione NON esiste. Gerarchia di
    // tentativi (mai un fallback non atomico che possa sovrascrivere):
    //   1. primitiva nativa per piattaforma: renameat2(RENAME_NOREPLACE) su
    //      Linux, renamex_np(RENAME_EXCL) su macOS, MoveFileExW senza
    //      MOVEFILE_REPLACE_EXISTING su Windows;
    //   2. se la primitiva non è disponibile sul filesystem (EINVAL/ENOSYS/
    //      EOPNOTSUPP, es. mount FUSE/fuseblk, NFS senza flag) tenta
    //      link()+unlink(), atomic e EEXIST-preserving;
    //   3. se anche gli hard link non sono supportati (es. FAT/exFAT/9p)
    //      ritorna NoAtomicSupport: fail-safe, il temporaneo resta al suo posto.
    // Su Exists il temporaneo resta al suo posto e il file finale è intatto.
    PublishStatus publishNoReplace(const std::string& tempPath, const std::string& finalPath);

#if defined(__linux__) || defined(__APPLE__)
    // Fallback del publish usato quando la primitiva no-replace è indisponibile
    // sul filesystem (EINVAL/ENOSYS/EOPNOTSUPP): link()+unlink() atomico e
    // EEXIST-preserving. Esposta in header per poterla testare direttamente.
    PublishStatus publishNoReplaceFallback(const std::string& tempPath,
                                           const std::string& finalPath);
#endif

} // namespace Core
