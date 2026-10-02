// Unit test per le funzioni pure del core (nessuna dipendenza da processi esterni).
// Eseguire con: ctest --test-dir build  (oppure ./build/test_core)
#include "config/AppConfigManager.hpp"
#include "core/FileUtils.hpp"
#include "core/InstanceLock.hpp"
#include "core/ProcessUtils.hpp"
#include "core/Series.hpp"
#include "core/SeriesRepository.hpp"
#include "core/ThumbCache.hpp"
#include "core/UpdateChecker.hpp"
#include "scrapers/ScraperUtils.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

static int g_failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            ++g_failures;                                                                          \
            std::cerr << "FAIL: " << #cond << " (riga " << __LINE__ << ")\n";                      \
        }                                                                                          \
    } while (0)

static void testCompareVersions() {
    using Core::UpdateChecker;

    CHECK(UpdateChecker::compareVersions("2.0.1", "2.0.0") > 0);
    CHECK(UpdateChecker::compareVersions("2.0.0", "2.0.0") == 0);
    CHECK(UpdateChecker::compareVersions("1.9.9", "2.0.0") < 0);
    CHECK(UpdateChecker::compareVersions("v2.0.1", "2.0.1") == 0);
    CHECK(UpdateChecker::compareVersions("2.0", "2.0.0") == 0);
    CHECK(UpdateChecker::compareVersions("2.0.10", "2.0.9") > 0);
    CHECK(UpdateChecker::compareVersions("10.0", "9.9.9") > 0);
}

static void testSeriesJsonRoundtrip() {
    Core::Series s;
    s.name = "Test";
    s.service = "animew";
    s.path = "/tmp/test";
    s.seriesPageUrl = "https://example.com/anime/test";
    s.lastDownloadedEpisode = 4;
    s.lastDownloadedAt = "2026-01-01T00:00:00Z";
    s.isHighPriority = true;
    s.alternateSources.push_back({"animeu", "https://example.com/u/test"});

    nlohmann::json j = s;
    Core::Series roundtrip = j.get<Core::Series>();
    CHECK(roundtrip == s);
    CHECK(roundtrip.lastDownloadedEpisode == 4);
    CHECK(roundtrip.isHighPriority == true);
    CHECK(roundtrip.alternateSources.size() == 1);
    CHECK(roundtrip.alternateSources[0].service == "animeu");
    CHECK(roundtrip.alternateSources[0].seriesPageUrl == "https://example.com/u/test");

    // Campi obbligatori mancanti -> from_json deve lanciare (contratto esplicito)
    bool threw = false;
    try {
        Core::Series bad = nlohmann::json{{"name", "x"}}.get<Core::Series>();
        (void)bad;
    } catch (const nlohmann::json::exception&) {
        threw = true;
    }
    CHECK(threw);
}

// Crea un file (sparse dove possibile) di dimensione logica data
static void createSizedFile(const std::filesystem::path& p, size_t size) {
    std::ofstream f(p, std::ios::binary);
    f.seekp(static_cast<std::streamoff>(size) - 1);
    f.write("\0", 1);
}

static void testScraperUtils() {
    using Core::ScraperUtils;

    // expandTilde: path senza tilde invariato
    CHECK(ScraperUtils::expandTilde("/tmp/x") == "/tmp/x");
#ifndef _WIN32
    // Q: quoting shell POSIX
    CHECK(ScraperUtils::Q("a b") == "'a b'");
    CHECK(ScraperUtils::Q("a'b") == "'a'\\''b'");
#endif

    // generateFilename: sostituisce il numero e mantiene il suffisso
    CHECK(ScraperUtils::generateFilename("https://site/v/file_ep_3_720p.mp4?x=1", 7) ==
          "file_ep_07_720p.mp4");
    CHECK(ScraperUtils::generateFilename("https://site/v/random.mp4", 3) == "random_Ep_03.mp4");

    // scanEpisodesMap / getHighestEpisodeFile su dir temporanea (file sparsi > 1MB)
    auto dir = std::filesystem::temp_directory_path() / "anidl_test_scan";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    createSizedFile(dir / "Serie_Ep_05.mp4", 1'100'000);
    createSizedFile(dir / "Serie_Ep_07.mp4", 1'100'000);
    createSizedFile(dir / "nota.txt", 1'100'000);     // nessun pattern Ep -> ignorato
    createSizedFile(dir / "piccolo_Ep_09.mp4", 1000); // < 1MB -> ignorato

    auto map = ScraperUtils::scanEpisodesMap(dir.string());
    CHECK(map.size() == 2);
    CHECK(map.count(5) == 1);
    CHECK(map.count(7) == 1);

    auto hi = ScraperUtils::getHighestEpisodeFile(dir.string());
    CHECK(hi.number == 7);

    // computeNextNeeded senza spawnare ffprobe (path inesistenti -> early exit)
    std::map<int, std::string> fake = {{1, "/nonexistent/x.mp4"}, {2, "/nonexistent/y.mp4"}};
    // Nessun episodio valido fino a last → riparte da last+1 (fix feature 11)
    CHECK(ScraperUtils::computeNextNeeded(dir.string(), 2, fake) == 3);
    // S4: media vuota + last=3 → prossimo episodio = 4 (non 1)
    auto emptyDir = dir / "vuota";
    std::filesystem::create_directories(emptyDir);
    CHECK(ScraperUtils::computeNextNeeded(emptyDir.string(), 3, fake) == 4);
    CHECK(ScraperUtils::computeNextNeeded("/nonexistent/dir", 0, {}) == 1);

    std::filesystem::remove_all(dir);
}

static void testSeriesRepository() {
    auto dir = std::filesystem::temp_directory_path() / "anidl_test_repo";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto jsonPath = dir / "series_data.json";

    Core::SeriesRepository repo(jsonPath);
    Core::Series s;
    s.name = "S1";
    s.service = "animew";
    s.seriesPageUrl = "https://example.com/s1";
    s.lastDownloadedEpisode = 2;
    repo.saveSeriesData({s});

    // applyDownloadedEpisodes: aggiorna episodio + timestamp e salva su disco
    bool ok = repo.applyDownloadedEpisodes({{"S1", 5}}, "2026-08-14T00:00:00Z");
    CHECK(ok);
    auto& list = repo.loadSeriesData(true);
    CHECK(list.size() == 1);
    CHECK(list[0].lastDownloadedEpisode == 5);
    CHECK(list[0].lastDownloadedAt == "2026-08-14T00:00:00Z");

    // map vuota -> nessuna modifica
    CHECK(!repo.applyDownloadedEpisodes({}, "x"));

    // serie sconosciuta -> nessuna modifica
    CHECK(!repo.applyDownloadedEpisodes({{"ZZ", 1}}, "x"));

    // episodio non più alto -> nessuna modifica (né episodio né timestamp)
    bool ok2 = repo.applyDownloadedEpisodes({{"S1", 1}}, "2026-08-14T00:00:01Z");
    CHECK(!ok2);
    CHECK(repo.loadSeriesData(true)[0].lastDownloadedEpisode == 5);
    CHECK(repo.loadSeriesData(true)[0].lastDownloadedAt == "2026-08-14T00:00:00Z");

    // stesso episodio -> nessuna scrittura spuria
    bool ok3 = repo.applyDownloadedEpisodes({{"S1", 5}}, "2026-08-14T00:00:02Z");
    CHECK(!ok3);
    CHECK(repo.loadSeriesData(true)[0].lastDownloadedAt == "2026-08-14T00:00:00Z");

    // avanzamento reale -> episodio e timestamp aggiornati insieme
    bool ok4 = repo.applyDownloadedEpisodes({{"S1", 7}}, "2026-08-14T00:00:03Z");
    CHECK(ok4);
    CHECK(repo.loadSeriesData(true)[0].lastDownloadedEpisode == 7);
    CHECK(repo.loadSeriesData(true)[0].lastDownloadedAt == "2026-08-14T00:00:03Z");

    std::filesystem::remove_all(dir);
}

// Lock esclusivo di processo con subprocess reale: il test esegue se stesso
// con --try-lock via ProcessUtils (cross-platform, esercita sia il ramo
// POSIX/flock sia quello Windows/CreateFile). O_CLOEXEC (POSIX) e handle non
// ereditabile (Windows) impediscono al figlio di "ereditare" il lock: il
// figlio riapre il path e la seconda acquisizione deve fallire.
static void testInstanceLock(const char* selfPath) {
    auto dir = std::filesystem::temp_directory_path() / "anidl_test_lock";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::string lockPath = (dir / "exec.lock").string();
    std::string cmd =
        Core::ScraperUtils::Q(selfPath) + " --try-lock " + Core::ScraperUtils::Q(lockPath);

    std::atomic<bool> stop(false);

    {
        Core::InstanceLock parentLock(lockPath);
        CHECK(parentLock.acquired());
        int status = Core::ProcessUtils::runCommand(cmd, stop);
        CHECK(status != 0); // il subprocess deve essere rifiutato
    }

    {
        // Lock rilasciato dal distruttore del parent → il subprocess acquisisce
        int status = Core::ProcessUtils::runCommand(cmd, stop);
        CHECK(status == 0); // il subprocess acquisisce e termina con successo
    }

    std::filesystem::remove_all(dir);
}

// Matrice di migrazione config feature 11 (vedi plan/11):
// clang-format off
// | auto_cleanup_on_close | resume_interrupted_downloads | Effetto |
// | assente               | assente → default true        | partials trattenuti (nuovo default) |
// | true esplicito        | assente                       | resume=false (comportamento vecchio preservato) |
// | false esplicito       | assente                       | resume=true (stesso comportamento) |
// | qualunque             | presente                      | la nuova chiave vince |
// clang-format on
static void testConfigMigration() {
    auto dir = std::filesystem::temp_directory_path() / "anidl_test_cfg";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
#ifdef _WIN32
    _putenv_s("XDG_CONFIG_HOME", dir.string().c_str());
#else
    setenv("XDG_CONFIG_HOME", dir.string().c_str(), 1);
#endif
    auto cfgPath = dir / "AniDownloader" / "config.json";
    std::filesystem::create_directories(cfgPath.parent_path());

    auto writeCfg = [&](const std::string& body) {
        std::ofstream f(cfgPath);
        f << body;
    };

    {
        // auto_cleanup=true esplicito (scelta deliberata di pulizia) → resume=false
        writeCfg(R"({"auto_cleanup_on_close":true})");
        Config::AppConfigManager m(cfgPath);
        CHECK(m.get<bool>("resume_interrupted_downloads", true) == false);
    }
    {
        // auto_cleanup=false esplicito (già tratteneva i partials) → resume=true
        writeCfg(R"({"auto_cleanup_on_close":false})");
        Config::AppConfigManager m(cfgPath);
        CHECK(m.get<bool>("resume_interrupted_downloads", true) == true);
    }
    {
        // chiave assente → nuovo default true (cambio = scopo della feature)
        writeCfg("{}");
        Config::AppConfigManager m(cfgPath);
        CHECK(m.get<bool>("resume_interrupted_downloads", true) == true);
    }
    {
        // la nuova chiave presente vince sempre
        writeCfg(R"({"auto_cleanup_on_close":true,"resume_interrupted_downloads":true})");
        Config::AppConfigManager m(cfgPath);
        CHECK(m.get<bool>("resume_interrupted_downloads", true) == true);
    }

    std::filesystem::remove_all(dir);
}

static void testPublishNoReplace() {
    using Core::PublishStatus;

    auto dir = std::filesystem::temp_directory_path() / "anidl_test_publish";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    // 1) Success: il temporaneo viene pubblicato col nome finale
    {
        auto tmp = dir / "ep.part";
        auto fin = dir / "ep.mp4";
        createSizedFile(tmp, 1'000);
        CHECK(Core::publishNoReplace(tmp.string(), fin.string()) == PublishStatus::Success);
        CHECK(std::filesystem::exists(fin));
        CHECK(!std::filesystem::exists(tmp));
        std::filesystem::remove(fin);
    }

    // 2) Exists: destinazione già presente → il temporaneo resta, finale intatto
    {
        auto tmp = dir / "ep2.part";
        auto fin = dir / "ep2.mp4";
        createSizedFile(tmp, 2'000);
        createSizedFile(fin, 3'000);
        CHECK(Core::publishNoReplace(tmp.string(), fin.string()) == PublishStatus::Exists);
        CHECK(std::filesystem::exists(tmp)); // mai cancellato su conflitto
        CHECK(std::filesystem::file_size(fin) == 3'000);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
    }

    // 3) Error: condizione reale (ENOENT, temporaneo sparito) distinta da
    //    NoAtomicSupport. Il finale non deve essere toccato.
    {
        auto tmp = dir / "ep3.part"; // non creato
        auto fin = dir / "ep3.mp4";
        createSizedFile(fin, 3'500);
        int publishErrno = 0;
        CHECK(Core::publishNoReplace(tmp.string(), fin.string(), &publishErrno) ==
              PublishStatus::Error);
#if defined(__linux__) || defined(__APPLE__)
        CHECK(publishErrno == ENOENT);
#endif
        CHECK(std::filesystem::file_size(fin) == 3'500);
        std::filesystem::remove(fin);
    }

    // 4) Fallback link+unlink (testata direttamente): esercita la funzione reale
    //    che in produzione va in azione quando il fs non supporta la primitiva
    //    rename no-replace (es. FUSE). Su un fs POSIX locale il comportamento è
    //    deterministico: successo senza sovrascritture, EEXIST preservato.
#if defined(__linux__) || defined(__APPLE__)
    {
        auto tmp = dir / "ep4.part";
        auto fin = dir / "ep4.mp4";
        createSizedFile(tmp, 4'000);
        CHECK(Core::publishNoReplaceFallback(tmp.string(), fin.string()) ==
              Core::PublishStatus::Success);
        CHECK(std::filesystem::exists(fin));
        CHECK(!std::filesystem::exists(tmp));
        std::filesystem::remove(fin);

        // EEXIST nel fallback: il finale già presente non viene toccato
        createSizedFile(tmp, 5'000);
        createSizedFile(fin, 6'000);
        CHECK(Core::publishNoReplaceFallback(tmp.string(), fin.string()) ==
              Core::PublishStatus::Exists);
        CHECK(std::filesystem::file_size(fin) == 6'000);
        CHECK(std::filesystem::exists(tmp));

        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
    }
#endif

    std::filesystem::remove_all(dir);
}

// ---- File picker: mount, creazione, rimozione ----

static void testParseMountTable() {
    // Fixture sintetica: i pseudo-fs vanno esclusi, FUSE/NFS vanno tenuti
    // (sono dischi veri navigabili), il path con spazio va decodificato.
    std::string table = "proc /proc proc rw,nosuid,relatime 0 0\n"
                        "sysfs /sys sysfs rw,relatime 0 0\n"
                        "tmpfs /run/user/1000 tmpfs rw 0 0\n"
                        "overlay /var/lib/docker/overlay2/x overlay rw 0 0\n"
                        "/dev/sda2 / ext4 rw,relatime 0 0\n"
                        "/dev/sdb1 /mnt/My\\040Backup ext4 rw,relatime 0 0\n"
                        "sshfs:/nas /mnt/nas fuse.sshfs rw 0 0\n"
                        "/dev/sdc1 /media/usb ntfs3 rw 0 0\n"
                        "binfmt_misc /proc/sys/fs/binfmt_misc binfmt_misc rw 0 0\n"
                        // Snap, boot e /home: volumi reali ma non da picker.
                        "/dev/loop1 /var/lib/snapd/snap/core22/2411 squashfs ro 0 0\n"
                        "/dev/loop2 /snap/bare/5 squashfs ro 0 0\n"
                        "/dev/sda1 /boot ext4 rw 0 0\n"
                        "/dev/sda3 /boot/efi vfat rw 0 0\n"
                        "/dev/sda4 /home ext4 rw 0 0\n";

    auto mounts = Core::parseMountTable(table);
    std::vector<std::string> paths;
    for (const auto& m : mounts)
        paths.push_back(m.path);

    auto has = [&](const std::string& p) {
        return std::find(paths.begin(), paths.end(), p) != paths.end();
    };
    auto nameOf = [&](const std::string& p) {
        for (const auto& m : mounts)
            if (m.path == p)
                return m.name;
        return std::string();
    };

    CHECK(has("/"));              // disco reale
    CHECK(has("/mnt/My Backup")); // escape octal \040 → spazio
    CHECK(has("/mnt/nas"));       // FUSE: disco navigabile
    CHECK(has("/media/usb"));     // rimovibile
    CHECK(!has("/proc"));         // pseudo-fs escluso
    CHECK(!has("/sys"));
    CHECK(!has("/run/user/1000"));                  // tmpfs escluso
    CHECK(!has("/var/lib/docker/overlay2/x"));      // overlay escluso
    CHECK(!has("/proc/sys/fs/binfmt_misc"));        // sotto /proc, escluso
    CHECK(!has("/var/lib/snapd/snap/core22/2411")); // snap: non e' un disco
    CHECK(!has("/snap/bare/5"));                    // snap: non e' un disco
    CHECK(!has("/boot"));                           // sistema, non da picker
    CHECK(!has("/boot/efi"));                       // sistema, non da picker
    CHECK(!has("/home")); // partizione coperta dalla voce "Home" della sidebar

    // Etichette leggibili: basename, non il path intero.
    CHECK(nameOf("/") == "/");
    CHECK(nameOf("/mnt/My Backup") == "My Backup");
    CHECK(nameOf("/media/usb") == "usb");

    // Bind mount duplicato: stesso path da due device -> una sola voce.
    // Escape non valido (\0X7: 'X' non e' octal): passa invariato.
    std::string table2 = std::string(table) + "/dev/sda2 / ext4 rw,relatime 0 0\n"
                                              "/dev/sdd1 /mnt/Bad\\0X7Escape ext4 rw 0 0\n"
                                              "riga malformata\n";

    auto mounts2 = Core::parseMountTable(table2);
    int rootCount = 0;
    bool hasBad = false;
    for (const auto& m : mounts2) {
        if (m.path == "/")
            ++rootCount;
        if (m.path == "/mnt/Bad\\0X7Escape")
            hasBad = true;
    }
    CHECK(rootCount == 1); // niente duplicati
    CHECK(hasBad);         // escape invalido: letterale, niente garbage

    // Tabella vuota: nessun mount, nessun crash.
    CHECK(Core::parseMountTable("").empty());
}

static void testParseAllMountPoints() {
    // Senza filtri: pseudo-fs e tmpfs/overlay sono comunque punti di mount
    // e il guardrail di removePath deve conoscerli tutti.
    std::string table = "proc /proc proc rw,nosuid,relatime 0 0\n"
                        "tmpfs /run/user/1000 tmpfs rw 0 0\n"
                        "overlay / overlay rw 0 0\n"
                        "/dev/sda2 / ext4 rw,relatime 0 0\n"
                        "/dev/sdb1 /mnt/My\\040Backup ext4 rw,relatime 0 0\n";
    auto points = Core::parseAllMountPoints(table);
    auto has = [&](const std::string& p) {
        return std::find(points.begin(), points.end(), p) != points.end();
    };
    CHECK(has("/proc"));
    CHECK(has("/run/user/1000"));
    CHECK(has("/"));
    CHECK(has("/mnt/My Backup")); // escape octal condiviso col parse filtrato
    CHECK(points.size() == 4);
    CHECK(Core::parseAllMountPoints("").empty());

    // listAllMountPoints() della macchina: include almeno "/" su Linux.
    auto live = Core::listAllMountPoints();
    CHECK(!live.empty());
    CHECK(std::find(live.begin(), live.end(), "/") != live.end());
}

static void testParseUserDirsFile() {
    std::string content = "# commento\n"
                          "XDG_DESKTOP_DIR=\"$HOME/Desktop\"\n"
                          "XDG_DOCUMENTS_DIR=\"$HOME/Documenti\"\n"
                          "XDG_DOWNLOAD_DIR=\"$HOME/Scaricati\"\n"
                          "XDG_MUSIC_DIR=\"$HOME/Musica\"\n"
                          "XDG_PICTURES_DIR=\"$HOME/Immagini\"\n"
                          "XDG_VIDEOS_DIR=\"$HOME/Video\"\n"
                          "XDG_TEMPLATES_DIR=\"$HOME/Modelli\"\n";
    auto places = Core::parseUserDirsFile(content, "/home/user");
    CHECK(places.size() == 6); // Modelli non e' una posizione del picker
    CHECK(places[0].id == "desktop");
    CHECK(places[0].path == "/home/user/Desktop");
    CHECK(places[0].name == "Desktop");
    CHECK(places[1].id == "documents");
    CHECK(places[1].path == "/home/user/Documenti");
    CHECK(places[1].name == "Documenti");
    // Relativo senza $HOME: da spec e' sotto $HOME.
    auto rel = Core::parseUserDirsFile("XDG_DOCUMENTS_DIR=\"Documenti\"\n", "/home/user");
    CHECK(rel.size() == 1);
    CHECK(rel[0].path == "/home/user/Documenti");
    // Directory disabilitata (puntata alla home stessa): saltata, non
    // mostrata come doppione della home (caso reale: XDG_DESKTOP_DIR="$HOME/").
    CHECK(Core::parseUserDirsFile("XDG_DESKTOP_DIR=\"$HOME/\"\n", "/home/user").empty());
    CHECK(Core::parseUserDirsFile("XDG_DESKTOP_DIR=\"$HOME\"\n", "/home/user").empty());
    // Righe malformate: ignorate senza crash.
    CHECK(Core::parseUserDirsFile("XDG_DOCUMENTS_DIR=\n", "/home/user").empty());
    CHECK(Core::parseUserDirsFile("", "/home/user").empty());
}

static void testListPlaces() {
    auto places = Core::listPlaces();
    // La home c'e' sempre (se $HOME esiste): prima voce, niente duplicati.
    if (const char* home = std::getenv("HOME")) {
        if (std::filesystem::is_directory(home)) {
            CHECK(!places.empty());
            CHECK(places[0].id == "home");
            CHECK(places[0].path == std::string(home));
            for (size_t i = 1; i < places.size(); ++i)
                CHECK(places[i].path != places[0].path);
        }
    }
    for (const auto& p : places) {
        CHECK(!p.id.empty());
        CHECK(!p.name.empty());
        CHECK(std::filesystem::is_directory(p.path));
    }
}

static void testBrowseParentPath() {
    CHECK(Core::browseParentPath("") == "");
    CHECK(Core::browseParentPath("/") == "");   // root: nessun parent
    CHECK(Core::browseParentPath("/a") == "/"); // "su" da /a
    CHECK(Core::browseParentPath("/a/b") == "/a");
    CHECK(Core::browseParentPath("relativa") == ""); // niente root: niente parent
}

static void testIsWindowsDriveRoot() {
    CHECK(Core::isWindowsDriveRoot("C:"));
    CHECK(Core::isWindowsDriveRoot("C:\\"));
    CHECK(Core::isWindowsDriveRoot("c:/"));
    CHECK(!Core::isWindowsDriveRoot("C:\\foo")); // non e' una radice
    CHECK(!Core::isWindowsDriveRoot("C:foo"));   // drive-relative: non e' una radice
    CHECK(!Core::isWindowsDriveRoot("/"));
    CHECK(!Core::isWindowsDriveRoot(""));
    CHECK(!Core::isWindowsDriveRoot("CC:"));
}

static void testListMounts() {
    auto mounts = Core::listMounts();
    // Deve esserci almeno la root, o il picker non potrebbe uscire dai mount.
    bool hasRoot = false;
    for (const auto& m : mounts)
        if (m.path == "/")
            hasRoot = true;
    CHECK(hasRoot);
    for (const auto& m : mounts)
        CHECK(!m.path.empty());
}

static void testFileOps() {
    using Core::FsOpStatus;
    auto dir = std::filesystem::temp_directory_path() / "anidl_test_fileops";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::string parent = dir.string();

    // --- Creazione directory ---
    std::string created;
    CHECK(Core::createDirectory(parent, "Nuova Serie", &created) == FsOpStatus::Ok);
    CHECK(std::filesystem::is_directory(created));
    // Nome duplicato → Exists
    CHECK(Core::createDirectory(parent, "Nuova Serie") == FsOpStatus::Exists);
    // Nomi invalidi (separatore, punto, vuoto, troppo lungo)
    CHECK(Core::createDirectory(parent, "a/b") == FsOpStatus::InvalidName);
    CHECK(Core::createDirectory(parent, "..") == FsOpStatus::InvalidName);
    CHECK(Core::createDirectory(parent, ".") == FsOpStatus::InvalidName);
    CHECK(Core::createDirectory(parent, "") == FsOpStatus::InvalidName);
    CHECK(Core::createDirectory(parent, std::string(300, 'x')) == FsOpStatus::InvalidName);
    CHECK(Core::createDirectory(parent, "bad\\name") == FsOpStatus::InvalidName);
    // Padre inesistente → NotFound
    CHECK(Core::createDirectory((dir / "ghost").string(), "X") == FsOpStatus::NotFound);

    // --- Creazione file vuoto ---
    std::string fileCreated;
    CHECK(Core::createEmptyFile(parent, "vuoto.mkv", &fileCreated) == FsOpStatus::Ok);
    CHECK(std::filesystem::is_regular_file(fileCreated));
    CHECK(std::filesystem::file_size(fileCreated) == 0);
    CHECK(Core::createEmptyFile(parent, "vuoto.mkv") == FsOpStatus::Exists);
    CHECK(Core::createEmptyFile(parent, "a/b.mkv") == FsOpStatus::InvalidName);

    // --- Listing con file ---
    auto dirsOnly = Core::listDirectories(parent);
    CHECK(dirsOnly.size() == 1);
    CHECK(dirsOnly[0].name == "Nuova Serie");
    CHECK(dirsOnly[0].isDir);

    auto withFiles = Core::listDirectories(parent, true);
    CHECK(withFiles.size() == 2);
    // Le directory vengono prima dei file, poi in ordine alfabetico.
    CHECK(withFiles[0].isDir);
    CHECK(withFiles[1].isDir == false);
    CHECK(withFiles[1].name == "vuoto.mkv");
    CHECK(withFiles[1].size == 0);

    // --- Rimozione: file e cartella vuota ---
    CHECK(Core::removePath(fileCreated, false) == FsOpStatus::Ok);
    CHECK(!std::filesystem::exists(fileCreated));
    CHECK(Core::removePath(fileCreated, false) == FsOpStatus::NotFound);
    CHECK(Core::removePath(created, false) == FsOpStatus::Ok);
    CHECK(!std::filesystem::exists(created));

    // --- Rimozione: cartella non vuota, prima e dopo recursive ---
    std::string nested;
    CHECK(Core::createDirectory(parent, "Con Contenuto", &nested) == FsOpStatus::Ok);
    Core::createEmptyFile(nested, "ep1.mkv");
    Core::createEmptyFile(nested, "ep2.mkv");
    std::filesystem::create_directories(std::filesystem::path(nested) / "sub");
    Core::createEmptyFile((std::filesystem::path(nested) / "sub").string(), "ep3.mkv");

    uint64_t count = 0;
    CHECK(Core::removePath(nested, false, &count) == FsOpStatus::NotEmpty);
    CHECK(count == 4);                      // 2 file + 1 subdir + 1 file dentro sub
    CHECK(std::filesystem::exists(nested)); // non vuota: intatta

    CHECK(Core::removePath(nested, true) == FsOpStatus::Ok);
    CHECK(!std::filesystem::exists(nested));

#ifndef _WIN32
    // --- Symlink: si rimuove il link, mai il bersaglio ---
    // (su Windows la creazione richiede privilegi: test solo POSIX)
    std::filesystem::path real = dir / "reale";
    std::filesystem::create_directories(real);
    Core::createEmptyFile(real.string(), "dentro.mkv");
    std::filesystem::path linkDir = dir / "linkdir";
    std::filesystem::create_directory_symlink(real, linkDir);
    // Senza recursive: il link si rimuove comunque (non e' una directory),
    // e il bersaglio con il suo contenuto resta intatto.
    CHECK(Core::removePath(linkDir.string(), false) == FsOpStatus::Ok);
    CHECK(!std::filesystem::exists(linkDir));
    CHECK(std::filesystem::exists(real / "dentro.mkv"));

    Core::createEmptyFile(parent, "reale.mkv");
    std::filesystem::path linkFile = dir / "linkfile.mkv";
    std::filesystem::create_symlink(dir / "reale.mkv", linkFile);
    CHECK(Core::removePath(linkFile.string(), false) == FsOpStatus::Ok);
    CHECK(!std::filesystem::exists(linkFile));
    CHECK(std::filesystem::exists(dir / "reale.mkv"));
    Core::removePath((dir / "reale.mkv").string(), false);
#endif

    // --- Guardrail: la root non è mai rimovibile ---
    CHECK(Core::removePath("/", true) == FsOpStatus::NotPermitted);
    CHECK(Core::removePath("", true) == FsOpStatus::InvalidName);
    // ... né il punto di mount della macchina (su Linux qualcuno esiste)
    for (const auto& m : Core::listMounts()) {
        if (m.path == "/")
            continue;
        CHECK(Core::removePath(m.path, true) == FsOpStatus::NotPermitted);
    }
    // ... e nemmeno quelli filtrati dalla UI (pseudo-fs, tmpfs, overlay):
    // sono comunque punti di mount, mai rimovibili.
    for (const auto& mp : Core::listAllMountPoints()) {
        if (mp == "/")
            continue;
        CHECK(Core::removePath(mp, true) == FsOpStatus::NotPermitted);
    }

    // --- espansione ~ (solo se HOME è impostato) ---
    if (const char* home = std::getenv("HOME")) {
        std::string expanded = Core::expandUserPath("~");
        CHECK(expanded == std::string(home));
        CHECK(Core::expandUserPath("/tmp") == "/tmp");
    }

    std::filesystem::remove_all(dir);
}

static void testThumbCache() {
    // --- selectThumbWidth: arrotonda al tier superiore, mai upscale oltre il max ---
    CHECK(Core::selectThumbWidth(-5) == 480);
    CHECK(Core::selectThumbWidth(0) == 480);
    CHECK(Core::selectThumbWidth(1) == 32);
    CHECK(Core::selectThumbWidth(32) == 32);
    CHECK(Core::selectThumbWidth(33) == 96);
    CHECK(Core::selectThumbWidth(96) == 96);
    CHECK(Core::selectThumbWidth(100) == 480);
    CHECK(Core::selectThumbWidth(480) == 480);
    CHECK(Core::selectThumbWidth(500) == 720);
    CHECK(Core::selectThumbWidth(720) == 720);
    CHECK(Core::selectThumbWidth(721) == 1080);
    CHECK(Core::selectThumbWidth(5000) == 1080);
    // --- clampThumbQuality: 1..100, fuori range -> default ---
    CHECK(Core::clampThumbQuality(0) == 80);
    CHECK(Core::clampThumbQuality(-3) == 80);
    CHECK(Core::clampThumbQuality(1) == 1);
    CHECK(Core::clampThumbQuality(100) == 100);
    CHECK(Core::clampThumbQuality(101) == 80);
    // --- chiave: deterministica e sensibile a ogni input ---
    const std::string k1 = Core::thumbCacheKey("/s/poster.jpg", 1000, 5000, 480, 80);
    CHECK(k1 == Core::thumbCacheKey("/s/poster.jpg", 1000, 5000, 480, 80));
    CHECK(!k1.empty());
    CHECK(k1 != Core::thumbCacheKey("/s/poster.jpg", 1001, 5000, 480, 80)); // mtime
    CHECK(k1 != Core::thumbCacheKey("/s/poster.jpg", 1000, 5001, 480, 80)); // size
    CHECK(k1 != Core::thumbCacheKey("/s/poster.jpg", 1000, 5000, 96, 80));  // w
    CHECK(k1 != Core::thumbCacheKey("/s/poster.jpg", 1000, 5000, 480, 90)); // q
    CHECK(k1 != Core::thumbCacheKey("/s/altro.jpg", 1000, 5000, 480, 80));  // path
    // --- eviction LRU: con tetto 250B su 3x100B cade solo il piu' vecchio ---
    auto dir = std::filesystem::temp_directory_path() / "anidl_test_thumbcap";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto now = std::filesystem::file_time_type::clock::now();
    const char* names[3] = {"vecchio.webp", "medio.webp", "nuovo.webp"};
    for (int i = 0; i < 3; ++i) {
        std::ofstream f(dir / names[i], std::ios::binary);
        f << std::string(100, static_cast<char>('a' + i));
        f.close();
        std::filesystem::last_write_time(dir / names[i], now - std::chrono::seconds(300 - 100 * i));
    }
    const auto removed = Core::enforceThumbCacheCap(dir.string(), 250);
    CHECK(removed.files == 1);
    CHECK(removed.bytes == 100);
    CHECK(!std::filesystem::exists(dir / "vecchio.webp"));
    CHECK(std::filesystem::exists(dir / "medio.webp"));
    CHECK(std::filesystem::exists(dir / "nuovo.webp"));
    // dir inesistente -> zero, mai eccezioni
    const auto empty = Core::enforceThumbCacheCap((dir / "ghost").string(), 10);
    CHECK(empty.files == 0 && empty.bytes == 0);
    std::filesystem::remove_all(dir);
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--try-lock") {
        Core::InstanceLock l(argv[2]);
        return l.acquired() ? 0 : 1;
    }

    testCompareVersions();
    testSeriesJsonRoundtrip();
    testScraperUtils();
    testSeriesRepository();
    testConfigMigration();
    testInstanceLock(argv[0]);
    testPublishNoReplace();
    testParseMountTable();
    testParseAllMountPoints();
    testParseUserDirsFile();
    testListPlaces();
    testBrowseParentPath();
    testIsWindowsDriveRoot();
    testListMounts();
    testFileOps();
    testThumbCache();

    if (g_failures == 0) {
        std::cout << "test_core: tutti i test superati\n";
        return 0;
    }
    std::cerr << "test_core: " << g_failures << " fallimenti\n";
    return 1;
}
