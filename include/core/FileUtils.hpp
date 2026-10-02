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

    // Espande "~" e "~/..." usando $HOME (su Windows anche %USERPROFILE%
    // se $HOME manca). Nessuna espansione oltre "~".
    std::string expandUserPath(const std::string& path);

    struct MountEntry {
        std::string name; // etichetta leggibile ("Disco locale (C:)", "Data")
        std::string path; // punto di mount
    };

    // Dischi/volumi attualmente montati, senza pseudo-filesystem.
    // Linux: /proc/self/mounts filtrato. Restano solo volumi utili
    // all'utente: niente pseudo-fs, tmpfs/overlay, squashfs (snap),
    // niente path di sistema (/proc, /sys, /dev, /snap, /boot,
    // /var/lib/{docker,containers,snapd}, /var/snap, /run tranne
    // /run/media) e niente /home (partizione coperta dalla voce "Home").
    // FUSE/NFS restano perche sono dischi veri. macOS: /Volumes.
    // Windows: GetLogicalDrives filtrato per tipo. In caso di errore
    // restituisce almeno la root "/" (il picker resta comunque utilizzabile).
    std::vector<MountEntry> listMounts();

    // Versione testabile e senza I/O di listMounts(): interpreta il contenuto
    // di un file /proc/self/mounts. Esposta in header per i test.
    // I punti di mount duplicati (bind mount) compaiono una sola volta.
    std::vector<MountEntry> parseMountTable(const std::string& content);

    // Tutti i punti di mount, SENZA filtri (uso interno: guardrail di
    // removePath). A differenza di listMounts(), include anche pseudo-fs e
    // tmpfs/overlay: un punto di mount non e' mai rimovibile, anche quando
    // non lo mostriamo nel picker.
    std::vector<std::string> parseAllMountPoints(const std::string& content);
    std::vector<std::string> listAllMountPoints();

    struct PlaceEntry {
        std::string id;   // "home", "desktop", "documents", ...
        std::string name; // etichetta ("Home", "Documenti", ...)
        std::string path;
    };

    // Posizioni principali dell'utente per la sidebar del picker (stile file
    // manager nativo): home + cartelle standard esistenti. Linux legge
    // ~/.config/user-dirs.dirs (con fallback ai candidati convenzionali);
    // macOS e Windows usano i percorsi convenzionali della home.
    // Solo directory esistenti, senza duplicati.
    std::vector<PlaceEntry> listPlaces();

    // Parsing puro di user-dirs.dirs (righe XDG_*_DIR="$HOME/..."), senza
    // I/O: esposta in header per i test.
    std::vector<PlaceEntry> parseUserDirsFile(const std::string& content, const std::string& home);

    // "C:", "C:\" e "C:/" sono radici di drive: mai rimovibili.
    // Funzione pura su stringa (testabile su ogni piattaforma), usata solo
    // su _WIN32.
    bool isWindowsDriveRoot(const std::string& path);

    // Parent per GET /api/browse: "" (= null nel JSON) se il path e' una
    // root ("/" su POSIX, "C:\" su Windows) o non ha parent calcolabile.
    // Il frontend usa questo valore per "su", senza splittare il path.
    std::string browseParentPath(const std::string& normalizedPath);

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
    // Rifiuta sempre la root, le radici dei drive, e TUTTI i punti di mount
    // (anche quelli filtrati dalla UI: vedi listAllMountPoints).
    // I symlink non vengono mai seguiti: viene eliminato il link, e il
    // pre-walk (conteggio, clear read-only) non tocca mai il bersaglio.
    FsOpStatus removePath(const std::string& path, bool recursive, uint64_t* outCount = nullptr);

    enum class PublishStatus { Success, Exists, NoAtomicSupport, Error };

    // Publish atomico no-replace (feature 11): pubblica il file temporaneo
    // con il nome finale solo se la destinazione NON esiste. Gerarchia di
    // tentativi (mai un fallback non atomico che possa sovrascrivere):
    //   1. primitiva nativa per piattaforma: renameat2(RENAME_NOREPLACE) su
    //      Linux, renamex_np(RENAME_EXCL) su macOS, MoveFileExA senza
    //      MOVEFILE_REPLACE_EXISTING su Windows;
    //   2. se la primitiva non è disponibile sul filesystem (EINVAL/ENOSYS/
    //      EOPNOTSUPP, es. mount FUSE/fuseblk, NFS senza flag) tenta
    //      link()+unlink(), atomic e EEXIST-preserving;
    //   3. se anche gli hard link non sono supportati (es. FAT/exFAT/9p)
    //      ritorna NoAtomicSupport: fail-safe, il temporaneo resta al suo posto.
    // Su Exists il temporaneo resta al suo posto e il file finale è intatto.
    // Error = condizione reale (ENOENT, EACCES, ENOSPC, EXDEV, EBUSY...):
    //            non è un problema di filesystem, e outErrno ne riporta la causa.
    PublishStatus publishNoReplace(const std::string& tempPath, const std::string& finalPath,
                                   int* outErrno = nullptr);

#if defined(__linux__) || defined(__APPLE__)
    // Fallback del publish usato quando la primitiva no-replace è indisponibile
    // sul filesystem (EINVAL/ENOSYS/EOPNOTSUPP): link()+unlink() atomico e
    // EEXIST-preserving. Esposta in header per poterla testare direttamente.
    PublishStatus publishNoReplaceFallback(const std::string& tempPath,
                                           const std::string& finalPath, int* outErrno = nullptr);
#endif

} // namespace Core
