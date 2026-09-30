#pragma once

#include <cstdint>
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
        bool isDir = true;
        int64_t size = 0; // byte, solo per i file
    };

    // Elenco del contenuto di dirPath. includeFiles=false (default storico)
    // restituisce solo le sottodirectory; includeFiles=true aggiunge anche i
    // file regolari (serve al file picker per poterli elencare e cancellare).
    // I nomi che iniziano con '.' restano esclusi. Ordinamento: directory
    // prima, poi file, ognun gruppo in ordine alfabetico.
    std::vector<DirEntry> listDirectories(const std::string& dirPath, bool includeFiles = false);

    // Espande "~" e "~/..." usando $HOME (nessun espansione oltre "~").
    std::string expandUserPath(const std::string& path);

    struct MountEntry {
        std::string name; // etichetta leggibile ("Disco locale (C:)", "Data")
        std::string path; // punto di mount
    };

    // Dischi/volumi attualmente montati, senza pseudo-filesystem.
    // Linux: /proc/self/mounts filtrato (niente proc/sys/dev/tmpfs/overlay...,
    // ma FUSE/NFS restano perche sono dischi veri). macOS: /Volumes.
    // Windows: GetLogicalDrives filtrato per tipo. In caso di errore
    // restituisce almeno la root "/" (il picker resta comunque utilizzabile).
    std::vector<MountEntry> listMounts();

    // Versione testabile e senza I/O di listMounts(): interpreta il contenuto
    // di un file /proc/self/mounts. Esposta in header per i test.
    std::vector<MountEntry> parseMountTable(const std::string& content);

    enum class FsOpStatus {
        Ok,
        InvalidName,  // nome vuoto, con separatori, "." o "..", troppo lungo
        Exists,       // il percorso esiste gia'
        NotFound,     // il padre non esiste o non e' una directory
        NotEmpty,     // directory non vuota con recursive=false
        NotPermitted, // permessi insufficienti o path protetto (root, mount point)
        IoError       // errore filesystem non classificato
    };

    // Crea parent/name come directory. name deve essere un nome singolo
    // (nessun separatore): la composizione avviene sempre lato server.
    FsOpStatus createDirectory(const std::string& parent, const std::string& name,
                               std::string* outPath = nullptr);
    FsOpStatus createEmptyFile(const std::string& parent, const std::string& name,
                               std::string* outPath = nullptr);

    // Rimuove path. Se recursive=false e path e' una directory non vuota
    // restituisce NotEmpty senza toccare nulla (in quel caso outCount, se
    // fornito, riceve il numero di elementi contenuti, con tetto a 10k).
    // Rifiuta sempre la root e i punti di mount (difesa rm -rf accidentale).
    FsOpStatus removePath(const std::string& path, bool recursive, uint64_t* outCount = nullptr);

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
