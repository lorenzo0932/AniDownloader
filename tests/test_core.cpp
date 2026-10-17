// Unit test per le funzioni pure del core (nessuna dipendenza da processi esterni).
// Eseguire con: ctest --test-dir build  (oppure ./build/test_core)
#include "config/AppConfigManager.hpp"
#include "core/CheckRunner.hpp"
#include "core/Database.hpp"
#include "core/DbImporter.hpp"
#include "core/ExecutionEngine.hpp"
#include "core/FileUtils.hpp"
#include "core/InstanceLock.hpp"
#include "core/ProcessUtils.hpp"
#include "core/QueueStore.hpp"
#include "core/Scheduler.hpp"
#include "core/Series.hpp"
#include "core/SeriesIdentity.hpp"
#include "core/SeriesRepository.hpp"
#include "core/SeriesUtils.hpp"
#include "core/ThumbCache.hpp"
#include "core/UpdateChecker.hpp"
#include "core/WorkerPool.hpp"
#include "scrapers/ScraperUtils.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
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

// Scrittura esterna vista senza forceReload: la cache si invalida su
// mismatch (mtime,size); il save aggiorna il tracking.
static void testCacheMtimeReload() {
    auto dir = std::filesystem::temp_directory_path() / "anidl_test_repo_mtime";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto jsonPath = dir / "series_data.json";

    Core::SeriesRepository repo(jsonPath);
    Core::Series s;
    s.name = "M1";
    s.service = "animew";
    s.seriesPageUrl = "https://example.com/m1";
    s.lastDownloadedEpisode = 2;
    repo.saveSeriesData({s});

    // load senza force: popola la cache
    CHECK(repo.loadSeriesData()[0].lastDownloadedEpisode == 2);
    // file invariato: la cache resta valida (nessun reload)
    CHECK(repo.loadSeriesData()[0].lastDownloadedEpisode == 2);

    // scrittore "esterno" (seconda istanza, come altro processo)
    Core::SeriesRepository writer(jsonPath);
    Core::Series ext = s;
    ext.lastDownloadedEpisode = 9;
    writer.saveSeriesData({ext});
    // mtime esplicito in avanti: non dipende dalla grana del FS
    std::error_code ec;
    auto cur = std::filesystem::last_write_time(jsonPath, ec);
    CHECK(!ec);
    std::filesystem::last_write_time(jsonPath, cur + std::chrono::seconds(10), ec);
    CHECK(!ec);

    // senza force deve vedere la modifica esterna
    CHECK(repo.loadSeriesData()[0].lastDownloadedEpisode == 9);

    // save proprio aggiorna il tracking: load senza force vede il salvato
    Core::Series upd = ext;
    upd.lastDownloadedEpisode = 10;
    repo.saveSeriesData({upd});
    CHECK(repo.loadSeriesData()[0].lastDownloadedEpisode == 10);

    // invalidateCache resetta anche il tracking: reload implicito
    repo.invalidateCache();
    CHECK(repo.loadSeriesData()[0].lastDownloadedEpisode == 10);

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

static void testBurstOnManualStartDefault() {
    auto dir = std::filesystem::temp_directory_path() / "anidl_test_burst_cfg";
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
        // Config storica senza la chiave → merge dei default: burst ON.
        writeCfg("{}");
        Config::AppConfigManager m(cfgPath);
        CHECK(m.get<bool>("burst_on_manual_start", false) == true);
    }
    {
        // Scelta utente esplicita dalle Impostazioni → vince sul default.
        writeCfg(R"({"burst_on_manual_start":false})");
        Config::AppConfigManager m(cfgPath);
        CHECK(m.get<bool>("burst_on_manual_start", true) == false);
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

static void testSortAdded() {
    // "added" non e un campo reale: vale l'ordine nel file.
    auto items = []() {
        return nlohmann::json::array({
            {{"name", "vecchia"}},
            {{"name", "media"}},
            {{"name", "nuova"}},
        });
    };
    // desc = piu recenti prima (ordine invertito)...
    auto d = items();
    Core::sortSeries(d, "added", true);
    CHECK(d[0].value("name", "") == "nuova");
    CHECK(d[2].value("name", "") == "vecchia");
    // ...asc = ordine file conservato.
    d = items();
    Core::sortSeries(d, "added", false);
    CHECK(d[0].value("name", "") == "vecchia");
    CHECK(d[2].value("name", "") == "nuova");
    // Campo sconosciuto: nessun effetto.
    d = items();
    Core::sortSeries(d, "nope", true);
    CHECK(d[0].value("name", "") == "vecchia");
    // Regressione: sort reale invariato (name desc alfabetico).
    d = items();
    Core::sortSeries(d, "name", true);
    CHECK(d[0].value("name", "") == "vecchia");
    CHECK(d[1].value("name", "") == "nuova");
    CHECK(d[2].value("name", "") == "media");
}

// Database: open/migrate idempotente, statement bind/step, transazioni
// commit/rollback. Fixture in tmp con cleanup sempre.
static void testDatabase() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "anidl_test_db";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    CHECK(!ec);

    // Open + migrate: schema v1, journal WAL.
    Core::Database db(dir / "test.db");
    CHECK(db.isOpen());
    CHECK(db.schemaVersion() == Core::Database::kSchemaVersion);
    {
        // Statement chiuso prima delle transazioni: un cursore di lettura
        // aperto bloccherebbe l'upgrade del lock al COMMIT.
        auto mode = db.prepare("PRAGMA journal_mode;");
        CHECK(mode.valid() && mode.step() && !mode.hasError());
        CHECK(mode.columnText(0) == "wal");
    }

    // Riapertura idempotente: stesso schema, nessun errore.
    {
        Core::Database reopen(dir / "test.db");
        CHECK(reopen.isOpen());
        CHECK(reopen.schemaVersion() == Core::Database::kSchemaVersion);
        std::string error;
        CHECK(reopen.migrate(&error));
    }

    // Migrazione v1 -> v2: simulo un DB fermo alla v1 (niente tasks) e
    // verifico che la riapertura crei la tabella senza toccare i dati.
    {
        CHECK(db.execute("INSERT INTO series(name) VALUES ('Vecchia');"));
        CHECK(db.execute("DROP TABLE tasks;"));
        CHECK(db.execute("DELETE FROM migrations WHERE version = 2;"));
    }
    {
        Core::Database migrato(dir / "test.db");
        CHECK(migrato.isOpen());
        CHECK(migrato.schemaVersion() == 2);
        auto sel = migrato.prepare("SELECT name FROM series WHERE name = 'Vecchia';");
        CHECK(sel.step() && sel.columnText(0) == "Vecchia");
        auto tasks = migrato.prepare("SELECT COUNT(*) FROM tasks;");
        CHECK(tasks.step() && tasks.columnInt(0) == 0);
    }

    // SQL errato: false + messaggio utile.
    {
        std::string error;
        CHECK(!db.execute("QUESTA NON E SQL VALIDO", &error));
        CHECK(!error.empty());
    }

    // Statement: insert con bind + select con colonne tipizzate.
    {
        auto ins = db.prepare("INSERT INTO kv(key, value) VALUES (?, ?);");
        CHECK(ins.valid());
        CHECK(ins.bindText(1, "chiave"));
        CHECK(ins.bindText(2, "valore"));
        CHECK(!ins.step());
        CHECK(!ins.hasError());
    }
    {
        auto sel = db.prepare("SELECT key, value FROM kv WHERE key = ?;");
        CHECK(sel.valid());
        CHECK(sel.bindText(1, "chiave"));
        CHECK(sel.step());
        CHECK(sel.columnCount() == 2);
        CHECK(sel.columnName(0) == "key");
        CHECK(sel.columnText(1) == "valore");
        CHECK(!sel.step());
        CHECK(!sel.hasError());
    }

    // Rollback: la riga scritta nel tx abortito non esiste.
    {
        Core::Transaction tx(db);
        CHECK(tx.active());
        CHECK(db.execute("INSERT INTO kv(key, value) VALUES ('fantasma', 'x');"));
    }
    {
        auto sel = db.prepare("SELECT COUNT(*) FROM kv WHERE key = 'fantasma';");
        CHECK(sel.step());
        CHECK(sel.columnInt(0) == 0);
    }

    // Commit: la riga resta.
    {
        Core::Transaction tx(db);
        CHECK(tx.active());
        CHECK(db.execute("INSERT INTO kv(key, value) VALUES ('reale', 'y');"));
        CHECK(tx.commit());
        CHECK(!tx.active());
    }
    {
        auto sel = db.prepare("SELECT value FROM kv WHERE key = 'reale';");
        CHECK(sel.step() && sel.columnText(0) == "y");
    }

    // Tabella series: insert minimo + vincolo chiave primaria.
    {
        std::string error;
        CHECK(
            db.execute("INSERT INTO series(name, service) VALUES ('Serie X', 'animew');", &error));
        CHECK(!db.execute("INSERT INTO series(name) VALUES ('Serie X');", &error));
    }

    fs::remove_all(dir, ec);
}

// DbImporter: fixture JSON → DB → ricontrollo totale; dry-run; JSON
// malformato; file mancante. Cleanup sempre.
static void testDbImporter() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "anidl_test_import";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    CHECK(!ec);

    const fs::path jsonPath = dir / "series_data.json";
    {
        // Fixture: 3 serie, una con fonti alternate e una con unicode.
        std::ofstream f(jsonPath);
        f << R"([
{"name": "Serie B", "service": "animew", "path": "/tmp/b",
 "continue": true, "is_high_priority": true, "passed_episodes": 2,
 "series_page_url": "https://x/b", "episode_list_selector": "l",
 "download_link_selector": "d", "last_downloaded_at": "2026-01-01",
 "last_downloaded_episode": 5,
 "alternate_sources": [{"service": "animeu", "series_page_url": "https://u/b"}]},
{"name": "Serie A", "service": "animeu", "path": "/tmp/a",
 "continue": false, "is_high_priority": false, "passed_episodes": 0,
 "series_page_url": "https://x/a", "episode_list_selector": "",
 "download_link_selector": "", "last_downloaded_at": "",
 "last_downloaded_episode": 0},
{"name": "Série C — àccénti", "service": "animew", "path": "/tmp/c",
 "continue": true, "is_high_priority": false, "passed_episodes": 0,
 "series_page_url": "https://x/c", "episode_list_selector": "",
 "download_link_selector": "", "last_downloaded_at": "",
 "last_downloaded_episode": 0}
])";
    }

    const fs::path dbPath = dir / "test.db";
    auto res = Core::DbImporter::importJson(jsonPath, dbPath);
    CHECK(res.ok);
    CHECK(res.error.empty());
    CHECK(res.stats.lette == 3);
    CHECK(res.stats.importate == 3);
    CHECK(res.stats.hashJson == res.stats.hashDb);
    CHECK(!res.backupPath.empty() && fs::exists(res.backupPath));
    {
        // Backup byte-identico all'originale.
        std::error_code sec;
        CHECK(fs::file_size(res.backupPath, sec) == fs::file_size(jsonPath, sec));
    }
    {
        // Rilettura diretta: fonti alternate sopravvissute al round-trip.
        Core::Database db(dbPath);
        CHECK(db.isOpen());
        auto sel = db.prepare("SELECT alternate_sources FROM series WHERE name='Serie B';");
        CHECK(sel.step());
        CHECK(sel.columnText(0).find("animeu") != std::string::npos);
    }

    // Dry-run: valida ma non scrive nulla (né DB né backup).
    {
        const fs::path dryJson = dir / "dry.json";
        {
            std::ofstream f(dryJson);
            f << R"([{"name": "Solo", "service": "s", "path": "p", "series_page_url": "u"}])";
        }
        auto dry = Core::DbImporter::importJson(dryJson, dir / "dry.db", true);
        CHECK(dry.ok);
        CHECK(dry.stats.lette == 1);
        CHECK(dry.backupPath.empty());
        CHECK(!fs::exists(dir / "dry.db"));
    }

    // JSON malformato: errore, nessun backup, nessun DB.
    {
        const fs::path badJson = dir / "bad.json";
        {
            std::ofstream f(badJson);
            f << R"([{"name": "rotta",)";
        }
        auto bad = Core::DbImporter::importJson(badJson, dir / "bad.db");
        CHECK(!bad.ok);
        CHECK(!bad.error.empty());
        CHECK(bad.backupPath.empty());
        CHECK(!fs::exists(dir / "bad.db"));
    }

    // File mancante: errore pulito.
    {
        auto missing = Core::DbImporter::importJson(dir / "inesistente.json", dir / "m.db");
        CHECK(!missing.ok);
        CHECK(!missing.error.empty());
    }

    fs::remove_all(dir, ec);
}

// Cutover (ADR-004 §6): il DB è la fonte; il JSON è export + fallback.
// Il mirror resta una verifica su ogni scrittura (divergenza contata,
// salvataggio non bloccato). Concorrenza: due handle in write-write non
// perdono righe. Cleanup sempre.
static void testShadowMode() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "anidl_test_shadow";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    CHECK(!ec);

    const fs::path jsonPath = dir / "series_data.json";
    const fs::path dbPath = Core::SeriesRepository::dbPathFor(jsonPath);
    CHECK(dbPath == dir / "series_data.db");
    CHECK(Core::SeriesRepository::dbPathFor("").empty());

    // Costruttore JSON-only: shadow spento, comportamento invariato.
    {
        Core::SeriesRepository soloJson(jsonPath);
        CHECK(!soloJson.shadowEnabled());
    }

    std::vector<Core::Series> due;
    {
        Core::Series a;
        a.name = "Alpha";
        a.service = "animew";
        a.path = "/tmp/a";
        a.seriesPageUrl = "https://x/a";
        Core::Series b = a;
        b.name = "Beta";
        due = {a, b};
    }

    Core::SeriesRepository repo(jsonPath, dbPath);
    CHECK(repo.shadowEnabled());
    CHECK(repo.shadowChecks() == 0);
    repo.saveSeriesData(due);
    CHECK(repo.shadowChecks() == 1);
    CHECK(repo.shadowDivergences() == 0);
    // Primo avvio senza export precedente: il mirror semina direttamente il
    // DB (nessun import, nessun .bak: il file non esisteva).
    CHECK(fs::exists(dbPath));
    {
        Core::Database db(dbPath);
        auto sel = db.prepare("SELECT COUNT(*) FROM series;");
        CHECK(sel.valid() && sel.step() && sel.columnInt(0) == 2);
    }
    // Letture dal DB autorevole (ordine righe = ordine array del JSON).
    auto lette = repo.loadSeriesData();
    CHECK(lette.size() == 2);
    CHECK(lette[0].name == "Alpha");

    // Modifica esterna al DB (altro processo): post-cutover è la verità, la
    // ricarica ne tiene conto grazie allo stat combinato di .db + -wal.
    {
        Core::Database db(dbPath);
        CHECK(db.execute("DELETE FROM series WHERE name='Beta';"));
    }
    {
        auto dati = repo.loadSeriesData();
        CHECK(dati.size() == 1); // DB autorevole: la cancellazione esterna vince
        repo.saveSeriesData(dati);
        CHECK(repo.shadowChecks() == 2);
        CHECK(repo.shadowDivergences() == 0); // mirror coerente con la fonte
    }
    {
        // DB rotto (tabella assente): la lettura casca sul export JSON
        // (comportamento pre-cutover) e la scrittura conta la divergenza
        // senza bloccare il salvataggio.
        Core::Database db(dbPath);
        CHECK(db.execute("DROP TABLE series;"));
    }
    {
        auto dati = repo.loadSeriesData();
        CHECK(dati.size() == 1); // fallback: l'export racconta l'ultimo stato
        repo.saveSeriesData(dati); // mirror fallisce, export intatto
        CHECK(repo.shadowDivergences() == 1);
        CHECK(repo.loadSeriesData().size() == 1);
    }

    fs::remove_all(dir, ec);
}

// Cutover DB fonte (ADR-004 §6): import automatico (DB vuoto + export con
// dati), letture dal DB con restart, export aggiornato a ogni save, recupero
// da export con DB svuotato, fallback con file DB illeggibile. Cleanup sempre.
static void testDbCutover() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "anidl_test_cutover";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    CHECK(!ec);

    const fs::path jsonPath = dir / "series_data.json";
    const fs::path dbPath = Core::SeriesRepository::dbPathFor(jsonPath);

    Core::Series a;
    a.name = "Alpha";
    a.service = "animew";
    a.path = "/tmp/a";
    a.seriesPageUrl = "https://x/a";
    a.passedEpisodes = 1;
    Core::Series b = a;
    b.name = "Beta";
    b.seriesPageUrl = "https://x/b";

    // 1) primo avvio: export pre-cutover con dati, DB assente → import auto + lettura dal DB.
    {
        nlohmann::json arr = std::vector<Core::Series>{a, b};
        std::ofstream f(jsonPath);
        f << arr.dump(4);
        f.close(); // il repository legge il file: nessun buffer in sospeso
        Core::SeriesRepository repo(jsonPath, dbPath);
        auto r = repo.loadSeriesData();
        CHECK(r.size() == 2);
        CHECK(r[0].name == "Alpha"); // ordine rowid = ordine atteso dall'API
        CHECK(fs::exists(dbPath));
    }

    // 2) restart simulato: nuova istanza leggale dal DB (righe > 0, niente import).
    {
        Core::SeriesRepository repo(jsonPath, dbPath);
        CHECK(repo.loadSeriesData().size() == 2);
    }

    // 3) ogni scrittura aggiorna anche l'export: addSeries → export con 3.
    {
        Core::SeriesRepository repo(jsonPath, dbPath);
        Core::Series c = a;
        c.name = "Gamma";
        c.seriesPageUrl = "https://x/c";
        auto esito = repo.addSeries(c);
        CHECK(esito.esito == Core::SeriesRepository::MutazioneEsito::Ok);
        std::ifstream f(jsonPath);
        auto arr = nlohmann::json::parse(f);
        CHECK(arr.is_array() && arr.size() == 3);
        CHECK(repo.loadSeriesData().size() == 3);
        CHECK(repo.shadowDivergences() == 0);
    }

    // 4) DB svuotato fuori dal Repository (disastro) → auto-import dall'export.
    {
        {
            Core::Database db(dbPath);
            CHECK(db.execute("DELETE FROM series;"));
        }
        Core::SeriesRepository repo(jsonPath, dbPath);
        CHECK(repo.loadSeriesData().size() == 3);
        CHECK(repo.shadowDivergences() == 0);
    }

    // 5) DB illeggibile (file non-SQLite) → fallback sull'export: lette e
    //    scritture proseguono senza perdita (mirror segnala la divergenza).
    {
        fs::rename(dbPath, dir / "db.sano.bak", ec);
        CHECK(!ec);
        {
            std::ofstream f(dbPath, std::ios::binary | std::ios::trunc);
            f << "questo non è un database SQLite";
            f.close(); // il repository apre il file: nessun buffer in sospeso
        }
        Core::SeriesRepository repo(jsonPath, dbPath);
        CHECK(repo.loadSeriesData().size() == 3);
        Core::Series d = a;
        d.name = "Delta";
        d.seriesPageUrl = "https://x/d";
        auto esito = repo.addSeries(d);
        CHECK(esito.esito == Core::SeriesRepository::MutazioneEsito::Ok); // salvato sull'export
        CHECK(repo.loadSeriesData().size() == 4);
        CHECK(repo.shadowDivergences() >= 1); // mirror impossibile: contatore
        // ripristino: export (4 serie) resta la copia di riferimento per il drill
        for (const char* suffix : {"", "-wal", "-shm"}) {
            std::error_code rm;
            fs::remove(fs::path(dbPath.string() + suffix), rm);
        }
        fs::rename(dir / "db.sano.bak", dbPath, ec);
        CHECK(!ec);
    }

    fs::remove_all(dir, ec);
}

static void testConcurrentWrites() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "anidl_test_conc";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    CHECK(!ec);

    // Due handle, write-write concorrente su chiavi distinte: alla fine
    // tutte le righe presenti, DB integro (niente lost update né lock persi).
    auto scrivi = [&](int base) {
        Core::Database db(dir / "conc.db");
        for (int i = 0; i < 25; ++i) {
            Core::Transaction tx(db);
            if (!tx.active())
                return false;
            auto ins = db.prepare("INSERT OR REPLACE INTO kv(key, value) VALUES (?, ?);");
            if (!ins.valid())
                return false;
            const std::string k = "k" + std::to_string(base + i);
            if (!ins.bindText(1, k) || !ins.bindText(2, "v") || (ins.step(), ins.hasError()))
                return false;
            if (!tx.commit())
                return false;
        }
        return true;
    };
    auto f1 = std::async(std::launch::async, scrivi, 0);
    auto f2 = std::async(std::launch::async, scrivi, 1000);
    CHECK(f1.get());
    CHECK(f2.get());
    {
        Core::Database db(dir / "conc.db");
        auto sel = db.prepare("SELECT COUNT(*) FROM kv;");
        CHECK(sel.step() && sel.columnInt(0) == 50);
    }

    fs::remove_all(dir, ec);
}

// QueueStore: enqueue/claim atomico, priorità, retry con backoff,
// crash recovery, conteggi per kind. Cleanup sempre.
static void testQueueStore() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "anidl_test_queue";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    CHECK(!ec);

    const fs::path dbPath = dir / "q.db";
    Core::QueueStore q(dbPath);
    CHECK(q.isOpen());

    // Coda vuota: nessun claim.
    CHECK(!q.claim().has_value());
    CHECK(q.pendingCount() == 0);

    // Priorità: il claim prende prima la più alta, poi FIFO.
    const auto idBassa = q.enqueue("check", "", 0, "{}", 0);
    const auto idAlta = q.enqueue("check", "", 0, "{}", 10);
    const auto idMedia = q.enqueue("check", "", 0, "{}", 5);
    CHECK(idBassa > 0 && idAlta > 0 && idMedia > 0);
    CHECK(q.pendingCount() == 3);
    CHECK(q.pendingCount("check") == 3);
    CHECK(q.pendingCount("download") == 0);
    auto primo = q.claim();
    CHECK(primo.has_value() && primo->id == idAlta);
    auto secondo = q.claim();
    CHECK(secondo.has_value() && secondo->id == idMedia);
    CHECK(q.pendingCount() == 1);
    // Task attivi non riclaimabili: terzo claim va sul rimanente.
    auto terzo = q.claim();
    CHECK(terzo.has_value() && terzo->id == idBassa);
    CHECK(!q.claim().has_value());
    CHECK(q.complete(idAlta) && q.complete(idMedia) && q.complete(idBassa));
    CHECK(q.pendingCount() == 0);

    // Retry con backoff: subito non riclaimabile, dopo scadenza sì.
    const auto idRetry = q.enqueue("check", "", 0, "{}", 0);
    auto t = q.claim();
    CHECK(t.has_value() && t->id == idRetry);
    CHECK(t->tentativi == 0);
    CHECK(q.failRetry(idRetry, 3600));
    CHECK(!q.claim().has_value());
    CHECK(q.pendingCount() == 1); // attesa futura conta come pending
    auto t2 = q.claim();          // ma non è ancora eseguibile
    CHECK(!t2.has_value());
    CHECK(q.failDead(idRetry));
    CHECK(q.pendingCount() == 0);

    // Crash recovery: task rimasto attivo torna in coda al reset.
    const auto idOrfano = q.enqueue("check", "Serie X", 3, "{}", 0);
    {
        auto preso = q.claim();
        CHECK(preso.has_value() && preso->serie == "Serie X" && preso->episodio == 3);
    }
    CHECK(!q.claim().has_value());
    CHECK(q.resetOrfani());
    {
        auto risorto = q.claim();
        CHECK(risorto.has_value() && risorto->id == idOrfano);
        CHECK(q.complete(idOrfano));
    }

    fs::remove_all(dir, ec);
}

// CheckRunner skip-paths (mai rete): libreria vuota e lock occupato.
// Scheduler: ciclo completo su libreria vuota senza toccare la rete
// (il giro trova zero serie e marca il task fallito). Cleanup sempre.
static void testScheduler() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "anidl_test_sched";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    CHECK(!ec);

    Config::AppConfigManager config(dir / "config.json");
    const std::string jsonPath = (dir / "series_data.json").string();
    std::atomic<bool> stop{false};

    // Libreria vuota: giro saltato, exit 0 equivalente.
    {
        Core::CheckOutcome esito = Core::CheckRunner::runOnce(config, jsonPath, false, stop);
        CHECK(!esito.eseguito);
        CHECK(esito.salto == Core::CheckOutcome::Salto::Vuoto);
    }

    // Lock occupato (da noi o dal demone: in entrambi i casi il giro si
    // rimanda): deterministico senza dipendere dallo stato esterno. Serve
    // una serie, altrimenti il giro salta prima per libreria vuota.
    {
        Core::SeriesRepository prepara(jsonPath, Core::SeriesRepository::dbPathFor(jsonPath));
        Core::Series s;
        s.name = "Blocca";
        s.service = "animew";
        s.path = "/tmp/blocca";
        s.seriesPageUrl = "https://x/blocca";
        prepara.saveSeriesData({s});
        Core::InstanceLock occupante((Config::PathHelper::getConfigDir() / "exec.lock").string());
        Core::CheckOutcome esito = Core::CheckRunner::runOnce(config, jsonPath, false, stop);
        CHECK(!esito.eseguito);
        CHECK(esito.salto == Core::CheckOutcome::Salto::Occupato);
    }

    // Scheduler spento: start/stop puliti, niente accodato.
    {
        // Reset libreria: i test dopo devono girare a zero serie (mai rete).
        for (const auto& entry : fs::directory_iterator(dir)) {
            const auto nome = entry.path().filename().string();
            if (nome.rfind("series_data", 0) == 0)
                fs::remove_all(entry.path(), ec);
        }
        config.set("scheduling", {{"abilitato", false}, {"intervalloMinuti", 15}});
        Core::Scheduler sched(config, jsonPath);
        CHECK(!sched.running());
        sched.start();
        CHECK(sched.running());
        std::this_thread::sleep_for(std::chrono::seconds(2));
        sched.stop();
        CHECK(!sched.running());
        Core::QueueStore coda(Core::SeriesRepository::dbPathFor(jsonPath));
        CHECK(coda.pendingCount() == 0);
    }

    // Scheduler acceso su libreria vuota: accoda un check (catch-up),
    // il worker lo esegue senza rete, task fallito, niente accumulo.
    {
        config.set("scheduling", {{"abilitato", true}, {"intervalloMinuti", 15}});
        Core::Scheduler sched(config, jsonPath);
        sched.start();
        Core::QueueStore coda(Core::SeriesRepository::dbPathFor(jsonPath));
        bool visto = false;
        for (int i = 0; i < 100 && !visto; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            Core::Database db(Core::SeriesRepository::dbPathFor(jsonPath));
            auto sel = db.prepare("SELECT COUNT(*) FROM tasks WHERE stato = 'fallito';");
            visto = sel.step() && !sel.hasError() && sel.columnInt(0) == 1;
        }
        sched.stop();
        CHECK(visto);
        CHECK(coda.pendingCount() == 0);
        // Secondo giro non riaccodato subito (ultimo_giro aggiornato).
        CHECK(coda.pendingCount("check") == 0);
    }

    fs::remove_all(dir, ec);
}

// WorkerPool: backoff progressivo poi resa, pausa, eventi, stop che attende
// il task in corso, niente doppio claim tra thread. Handler fake: mai rete.
static void testWorkerPool() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "anidl_test_pool";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    CHECK(!ec);
    const std::string db = (dir / "p.db").string();

    auto statoTask = [&](std::int64_t id) {
        Core::Database d(dir / "p.db");
        auto sel = d.prepare("SELECT stato, tentativi FROM tasks WHERE id = ?;");
        sel.bindInt(1, id);
        sel.step();
        return std::pair<std::string, int>(sel.columnText(0), static_cast<int>(sel.columnInt(1)));
    };
    auto attendiStato = [&](std::int64_t id, const std::string& stato) {
        for (int i = 0; i < 100; ++i) {
            if (statoTask(id).first == stato)
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    };

    // Backoff: una riprova riprogramma a ~5' con tentativi=1; a tentativi
    // esauriti (3) la successiva riprova diventa fallito definitivo.
    // (I 5'/15'/1h reali non si attendono: l'escalation è guidata via SQL.)
    {
        Core::QueueStore coda(dir / "p.db");
        const auto id = coda.enqueue("lavoro", "", 0, "{}", 0);
        CHECK(id > 0);
        Core::WorkerPool pool(db, 1);
        pool.onKind("lavoro", [](const Core::QueueTask&) { return Core::EsitoTask::Riprova; });
        pool.start();
        bool riprogrammato = false;
        for (int i = 0; i < 100 && !riprogrammato; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const auto st = statoTask(id);
            riprogrammato = (st.first == "attesa" && st.second == 1);
        }
        CHECK(riprogrammato);
        pool.stop();
        // Prossima esecuzione ~300s nel futuro (primo scaglione).
        {
            Core::Database d(dir / "p.db");
            auto sel = d.prepare("SELECT prossima_esecuzione FROM tasks WHERE id = ?;");
            sel.bindInt(1, id);
            CHECK(sel.step());
            const auto ora = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
            CHECK(sel.columnInt(0) > ora + 200);
            CHECK(sel.columnInt(0) < ora + 400);
        }
        // Simulo 3 tentativi consumati: la prossima riprova chiude fallito.
        {
            Core::Database d(dir / "p.db");
            CHECK(d.execute("UPDATE tasks SET tentativi = 3, prossima_esecuzione = 0, "
                            "stato = 'attesa' WHERE id = " +
                            std::to_string(id) + ";"));
        }
        pool.start();
        CHECK(attendiStato(id, "fallito"));
        pool.stop();
        CHECK(statoTask(id).second == 3);
    }

    // Pausa: niente claim da fermo; resume riprende. Eventi osservati.
    {
        Core::QueueStore coda(dir / "p.db");
        const auto id = coda.enqueue("lavoro", "", 0, "{}", 0);
        std::vector<std::string> eventi;
        std::mutex mutexEventi;
        Core::WorkerPool pool(db, 1);
        pool.onKind("lavoro", [](const Core::QueueTask&) { return Core::EsitoTask::Completato; });
        pool.onEvento([&](const std::string& tipo, const Core::QueueTask&) {
            std::lock_guard<std::mutex> l(mutexEventi);
            eventi.push_back(tipo);
        });
        pool.pausa(true);
        pool.start();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        CHECK(statoTask(id).first == "attesa"); // fermo in pausa
        pool.pausa(false);
        CHECK(attendiStato(id, "fatto"));
        pool.stop();
        std::lock_guard<std::mutex> l(mutexEventi);
        CHECK(std::find(eventi.begin(), eventi.end(), "avviato") != eventi.end());
        CHECK(std::find(eventi.begin(), eventi.end(), "completato") != eventi.end());
    }

    // Due thread: 10 task, mai doppio claim, tutti fatti una volta.
    {
        Core::QueueStore coda(dir / "p.db");
        for (int i = 0; i < 10; ++i)
            coda.enqueue("lavoro", "", 0, "{}", 0);
        std::atomic<int> eseguiti{0};
        Core::WorkerPool pool(db, 2);
        pool.onKind("lavoro", [&](const Core::QueueTask& t) {
            (void)t;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            ++eseguiti;
            return Core::EsitoTask::Completato;
        });
        pool.start();
        for (int i = 0; i < 100 && eseguiti.load() < 10; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        pool.stop();
        CHECK(eseguiti.load() == 10);
        CHECK(coda.pendingCount() == 0);
    }

    // Stop cooperativo: attende il task in corso (2s) invece di ucciderlo.
    {
        Core::QueueStore coda(dir / "p.db");
        const auto id = coda.enqueue("lento", "", 0, "{}", 0);
        Core::WorkerPool pool(db, 1);
        pool.onKind("lento", [](const Core::QueueTask&) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            return Core::EsitoTask::Completato;
        });
        pool.start();
        CHECK(attendiStato(id, "attivo") || statoTask(id).first == "fatto");
        const auto inizio = std::chrono::steady_clock::now();
        pool.stop();
        const auto atteso = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - inizio)
                                .count();
        CHECK(statoTask(id).first == "fatto");
        (void)atteso; // lo stop ha atteso: il task è fatto, non orfano
    }

    fs::remove_all(dir, ec);
}

static void testNfoPosterOnce() {
    auto dir = std::filesystem::temp_directory_path() / "anidl_test_nfo";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "Serie Pro&va <test>");

    std::string serie = (dir / "Serie Pro&va <test>").string();
    Core::NfoData nfo;
    nfo.trama = "Trama con & e <tag>.";
    nfo.anno = "2007";
    nfo.stato = "Finito";
    nfo.generi = {"Azione", "Avventura"};

    // Prima scrittura: ok, file Kodi valido con escape XML
    CHECK(Core::writeTvshowNfoOnce(serie, "Serie Pro&va <test>", nfo));
    std::string content;
    {
        std::ifstream f(dir / "Serie Pro&va <test>" / "tvshow.nfo");
        std::ostringstream ss;
        ss << f.rdbuf();
        content = ss.str();
    }
    CHECK(content.find("<title>Serie Pro&amp;va &lt;test&gt;</title>") != std::string::npos);
    CHECK(content.find("<plot>Trama con &amp; e &lt;tag&gt;.</plot>") != std::string::npos);
    CHECK(content.find("<genre>Azione</genre>") != std::string::npos);
    CHECK(content.find("<year>2007</year>") != std::string::npos);
    CHECK(content.find("<status>Finito</status>") != std::string::npos);
    CHECK(content.find("<studio>AnimeWorld</studio>") != std::string::npos);
    // Roundtrip via lettore esistente
    CHECK(Core::readNfoDescription(serie) == "Trama con & e <tag>.");

    // Seconda scrittura: mai sovrascrivere (contenuto intatto)
    Core::NfoData altro;
    altro.trama = "Altra trama";
    CHECK(!Core::writeTvshowNfoOnce(serie, "Altro", altro));
    CHECK(Core::readNfoDescription(serie) == "Trama con & e <tag>.");

    // Poster: scrive una volta sola
    CHECK(Core::savePosterOnce(serie, std::string(2048, 'J')));
    CHECK(std::filesystem::file_size(dir / "Serie Pro&va <test>" / "folder.jpg") == 2048);
    CHECK(!Core::savePosterOnce(serie, std::string(10, 'X')));
    CHECK(std::filesystem::file_size(dir / "Serie Pro&va <test>" / "folder.jpg") == 2048);

    std::filesystem::remove_all(dir);
}

// Identità serie (B0): relativo AW == assoluto canonico, host
// case-insensitive, path case-sensitive, episodio mai collassato.
static void testSeriesIdentity() {
    using Core::discoverIdentity;
    using Core::seriesIdentity;

    // Relativo del catalogo == assoluto su host ufficiale.
    CHECK(seriesIdentity("animeW_scraper", "/play/ghost-meets-gal.0H0eO") ==
          seriesIdentity("animeW_scraper", "https://www.animeworld.ac/play/ghost-meets-gal.0H0eO"));
    CHECK(discoverIdentity("/play/ghost-meets-gal.0H0eO") ==
          seriesIdentity("animeW_scraper", "https://www.animeworld.ac/play/ghost-meets-gal.0H0eO"));

    // Host maiuscolo/minuscolo equivale...
    CHECK(seriesIdentity("animeW_scraper", "https://WWW.ANIMEWORLD.AC/play/ghost-meets-gal.0H0eO") ==
          seriesIdentity("animeW_scraper", "https://www.animeworld.ac/play/ghost-meets-gal.0H0eO"));
    // ...ma il path che cambia case NON equivale (ID con maiuscole/minuscole).
    CHECK(seriesIdentity("animeW_scraper", "https://www.animeworld.ac/play/Ghost-Meets-Gal.0H0eO") !=
          seriesIdentity("animeW_scraper", "https://www.animeworld.ac/play/ghost-meets-gal.0H0eO"));

    // Mirror .so verificato (301 a .ac) equivale al canonico.
    CHECK(seriesIdentity("animeW_scraper", "https://www.animeworld.so/play/ghost-meets-gal.0H0eO") ==
          seriesIdentity("animeW_scraper", "https://www.animeworld.ac/play/ghost-meets-gal.0H0eO"));
    // Host estraneo mai equivalente.
    CHECK(seriesIdentity("animeW_scraper", "https://evil.example.com/play/ghost-meets-gal.0H0eO") !=
          seriesIdentity("animeW_scraper", "https://www.animeworld.ac/play/ghost-meets-gal.0H0eO"));

    // Trailing slash / fragment / query non cambiano l'identità serie.
    CHECK(seriesIdentity("animeW_scraper", "https://www.animeworld.ac/play/ghost-meets-gal.0H0eO/") ==
          seriesIdentity("animeW_scraper", "https://www.animeworld.ac/play/ghost-meets-gal.0H0eO"));
    CHECK(seriesIdentity("animeW_scraper",
                         "https://www.animeworld.ac/play/ghost-meets-gal.0H0eO#trama") ==
          seriesIdentity("animeW_scraper", "https://www.animeworld.ac/play/ghost-meets-gal.0H0eO"));

    // URL episodio NON diventa identità della serie.
    CHECK(seriesIdentity("animeW_scraper",
                         "https://www.animeworld.ac/play/ghost-meets-gal.0H0eO/abc123") !=
          seriesIdentity("animeW_scraper", "https://www.animeworld.ac/play/ghost-meets-gal.0H0eO"));

    // Stesso path su servizi diversi resta diverso; stesso titolo non conta.
    CHECK(seriesIdentity("animeW_scraper", "https://www.animeworld.ac/play/ghost-meets-gal.0H0eO") !=
          seriesIdentity("animeU_scraper", "https://www.animeworld.ac/play/ghost-meets-gal.0H0eO"));
    CHECK(seriesIdentity("animeU_scraper", "https://animeunity.to/watch/ghost/1") !=
          seriesIdentity("animeU_scraper", "https://animeunity.to/watch/gal/1"));

    // AU: mirror storico .so equivale al canonico .to, host case-insensitive.
    CHECK(seriesIdentity("animeU_scraper", "https://animeunity.so/watch/ghost/1") ==
          seriesIdentity("animeU_scraper", "https://animeunity.to/watch/ghost/1"));

    // Vuoti mai validi.
    CHECK(seriesIdentity("", "https://www.animeworld.ac/play/x.y").empty());
    CHECK(seriesIdentity("animeW_scraper", "").empty());
    CHECK(seriesIdentity("animeW_scraper", "   ").empty());
}

// Mutazioni atomiche del repository: vincolo anti-duplicato, concorrenza con
// barriera (mai sleep), nessun bypass read-modify-save, errori propagati.
static void testRepositoryAtomic() {
    using Core::SeriesRepository;
    auto dir = std::filesystem::temp_directory_path() / "anidl_test_repo_atomic";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto jsonPath = dir / "series_data.json";

    auto makeSeries = [](const std::string& name, const std::string& url) {
        Core::Series s;
        s.name = name;
        s.service = "animeW_scraper";
        s.path = "/tmp/" + name;
        s.seriesPageUrl = url;
        return s;
    };

    SeriesRepository repo(jsonPath);
    const std::string urlA = "https://www.animeworld.ac/play/serie-a.AAAA1";
    const std::string urlB = "https://www.animeworld.ac/play/serie-b.BBBB2";

    // Add ok con identity; mirror .so considerato duplicato.
    auto addA = repo.addSeries(makeSeries("A", urlA));
    CHECK(addA.esito == SeriesRepository::MutazioneEsito::Ok);
    CHECK(addA.indice == 0);
    CHECK(!addA.identity.empty());
    CHECK(addA.identity ==
          Core::seriesIdentity("animeW_scraper", "https://www.animeworld.so/play/serie-a.AAAA1"));
    auto dupA = repo.addSeries(makeSeries("A-copia", "https://www.animeworld.so/play/serie-a.AAAA1/"));
    CHECK(dupA.esito == SeriesRepository::MutazioneEsito::Duplicato);
    CHECK(dupA.indice == 0);
    CHECK(repo.snapshotSeriesData().size() == 1);
    CHECK(repo.shadowChecks() == 0); // JSON-only: nessun mirror aggiunto

    // Update stessa serie/stessa identity ammesso (correzione altri campi).
    Core::Series aMod = makeSeries("A", urlA);
    aMod.path = "/tmp/A-nuovo";
    auto updSame = repo.updateSeries(0, aMod);
    CHECK(updSame.esito == SeriesRepository::MutazioneEsito::Ok);
    CHECK(repo.snapshotSeriesData()[0].path == "/tmp/A-nuovo");

    // Update verso identità di un'altra serie rifiutato, senza modifiche.
    CHECK(repo.addSeries(makeSeries("B", urlB)).esito == SeriesRepository::MutazioneEsito::Ok);
    Core::Series bConflict = makeSeries("B", urlA);
    auto updConf = repo.updateSeries(1, bConflict);
    CHECK(updConf.esito == SeriesRepository::MutazioneEsito::Duplicato);
    CHECK(repo.snapshotSeriesData()[1].seriesPageUrl == urlB);
    // Indice assente: 404 logico.
    CHECK(repo.updateSeries(9, makeSeries("X", urlB)).esito ==
          SeriesRepository::MutazioneEsito::NonTrovato);
    CHECK(repo.removeSeries(9).esito == SeriesRepository::MutazioneEsito::NonTrovato);

    // Due thread, stessa identity, partenza sincronizzata: uno Ok, uno Duplicato.
    {
        auto dir2 = std::filesystem::temp_directory_path() / "anidl_test_repo_race";
        std::filesystem::remove_all(dir2);
        std::filesystem::create_directories(dir2);
        SeriesRepository race(dir2 / "series_data.json");
        std::barrier sync(2);
        SeriesRepository::AddEsito r1, r2;
        std::thread t1([&] {
            sync.arrive_and_wait();
            r1 = race.addSeries(makeSeries("R", urlA));
        });
        std::thread t2([&] {
            sync.arrive_and_wait();
            r2 = race.addSeries(makeSeries("R-bis", urlA));
        });
        t1.join();
        t2.join();
        int okCount = (r1.esito == SeriesRepository::MutazioneEsito::Ok ? 1 : 0) +
                      (r2.esito == SeriesRepository::MutazioneEsito::Ok ? 1 : 0);
        int dupCount = (r1.esito == SeriesRepository::MutazioneEsito::Duplicato ? 1 : 0) +
                       (r2.esito == SeriesRepository::MutazioneEsito::Duplicato ? 1 : 0);
        CHECK(okCount == 1);
        CHECK(dupCount == 1);
        CHECK(race.snapshotSeriesData().size() == 1);
        std::filesystem::remove_all(dir2);
    }

    // Due add diverse concorrenti: entrambe presenti.
    {
        auto dir3 = std::filesystem::temp_directory_path() / "anidl_test_repo_race2";
        std::filesystem::remove_all(dir3);
        std::filesystem::create_directories(dir3);
        SeriesRepository race(dir3 / "series_data.json");
        std::barrier sync(2);
        SeriesRepository::AddEsito r1, r2;
        std::thread t1([&] {
            sync.arrive_and_wait();
            r1 = race.addSeries(makeSeries("R1", urlA));
        });
        std::thread t2([&] {
            sync.arrive_and_wait();
            r2 = race.addSeries(makeSeries("R2", urlB));
        });
        t1.join();
        t2.join();
        CHECK(r1.esito == SeriesRepository::MutazioneEsito::Ok);
        CHECK(r2.esito == SeriesRepository::MutazioneEsito::Ok);
        CHECK(race.snapshotSeriesData().size() == 2);
        std::filesystem::remove_all(dir3);
    }

    // Remove + avanzamento: nessuna resurrezione della serie rimossa.
    CHECK(repo.removeSeries(0).esito == SeriesRepository::MutazioneEsito::Ok);
    CHECK(repo.snapshotSeriesData().size() == 1);
    CHECK(repo.applyDownloadedEpisodes({{"B", 3}}, "2026-10-12T00:00:00Z"));
    auto snap = repo.snapshotSeriesData();
    CHECK(snap.size() == 1);
    CHECK(snap[0].name == "B");
    CHECK(snap[0].lastDownloadedEpisode == 3);

    // Snapshot precedente resta valido dopo nuove scritture.
    auto prima = repo.snapshotSeriesData();
    CHECK(repo.addSeries(makeSeries("C", "https://www.animeworld.ac/play/serie-c.CCCC3")).esito ==
          SeriesRepository::MutazioneEsito::Ok);
    CHECK(prima.size() == 1);
    CHECK(repo.snapshotSeriesData().size() == 2);

    // Errore storage deterministico (parent del JSON è un file regolare):
    // esito tipizzato, cache/dati vecchi integri, niente throw.
    {
        auto dir4 = std::filesystem::temp_directory_path() / "anidl_test_repo_err";
        std::filesystem::remove_all(dir4);
        std::filesystem::create_directories(dir4);
        auto blocker = dir4 / "blocco";
        {
            std::ofstream f(blocker);
            f << "non una directory";
        }
        SeriesRepository rotto(blocker / "series_data.json");
        auto bad = rotto.addSeries(makeSeries("E", urlA));
        CHECK(bad.esito == SeriesRepository::MutazioneEsito::ErroreStorage);
        CHECK(!bad.errore.empty());
        std::filesystem::remove_all(dir4);
    }

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
    testSeriesIdentity();
    testRepositoryAtomic();
    testCacheMtimeReload();
    testConfigMigration();
    testBurstOnManualStartDefault();
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
    testNfoPosterOnce();
    testThumbCache();
    testSortAdded();
    testDatabase();
    testDbImporter();
    testShadowMode();
    testDbCutover();
    testConcurrentWrites();
    testQueueStore();
    testScheduler();
    testWorkerPool();

    if (g_failures == 0) {
        std::cout << "test_core: tutti i test superati\n";
        return 0;
    }
    std::cerr << "test_core: " << g_failures << " fallimenti\n";
    return 1;
}
