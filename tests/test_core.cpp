// Unit test per le funzioni pure del core (nessuna dipendenza da processi esterni).
// Eseguire con: ctest --test-dir build  (oppure ./build/test_core)
#include "core/InstanceLock.hpp"
#include "core/ProcessUtils.hpp"
#include "core/Series.hpp"
#include "core/SeriesRepository.hpp"
#include "core/UpdateChecker.hpp"
#include "scrapers/ScraperUtils.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

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
    std::string cmd = Core::ScraperUtils::Q(selfPath) + " --try-lock " +
                      Core::ScraperUtils::Q(lockPath);

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

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--try-lock") {
        Core::InstanceLock l(argv[2]);
        return l.acquired() ? 0 : 1;
    }

    testCompareVersions();
    testSeriesJsonRoundtrip();
    testScraperUtils();
    testSeriesRepository();
    testInstanceLock(argv[0]);

    if (g_failures == 0) {
        std::cout << "test_core: tutti i test superati\n";
        return 0;
    }
    std::cerr << "test_core: " << g_failures << " fallimenti\n";
    return 1;
}
