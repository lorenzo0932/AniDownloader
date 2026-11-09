// Test unitari della scelta del workdir di conversione: stima dello spazio,
// ordine RAM → cartella serie → cache e pulizia dei residui orfani.
// Non richiede ffmpeg/aria2: nessuna conversione reale.
#include "config/PathHelper.hpp"
#include "core/FileUtils.hpp"
#include "core/MediaProcessor.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int g_failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK fallito: " #cond "\n";            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

static void testEstimateConversionBytes() {
    // 1.56× a passaggio singolo, 2.86× in modalità chunk (margine incluso).
    CHECK(Core::estimateConversionBytes(0, 1) == 0);
    CHECK(Core::estimateConversionBytes(100, 1) == 156);
    CHECK(Core::estimateConversionBytes(100, 4) == 286);
    CHECK(Core::estimateConversionBytes(1000, 0) == 1560);
    CHECK(Core::estimateConversionBytes(1000, 2) == 2860);
    CHECK(Core::estimateConversionBytes(1000, 2) >
          Core::estimateConversionBytes(1000, 1));
}

static void testChooseWorkRoot() {
    const auto base = fs::temp_directory_path() / "anidl_test_workroot";
    fs::remove_all(base);
    fs::create_directories(base / "ram");
    fs::create_directories(base / "series");
    fs::create_directories(base / "cache");

    const std::vector<fs::path> ram{base / "ram"};
    const std::vector<fs::path> noRam;

    // Con spazio ovunque vince la prima radice RAM.
    auto scelto = Core::chooseWorkRoot(ram, base / "series", base / "cache", 1);
    CHECK(scelto.has_value());
    CHECK(*scelto == base / "ram");

    // Senza RAM si passa alla cartella serie (rename atomico).
    scelto = Core::chooseWorkRoot(noRam, base / "series", base / "cache", 1);
    CHECK(scelto.has_value());
    CHECK(*scelto == base / "series");

    // Serie assente → cache.
    scelto = Core::chooseWorkRoot(noRam, base / "assente", base / "cache", 1);
    CHECK(scelto.has_value());
    CHECK(*scelto == base / "cache");

    // Nessuna candidata esistente → nessuna scelta.
    CHECK(!Core::chooseWorkRoot(noRam, base / "assente", base / "assente2", 1).has_value());

    // Fabbisogno irraggiungibile → nessuna scelta, anche con radici esistenti.
    const auto enorme = std::numeric_limits<std::uint64_t>::max() / 2;
    CHECK(!Core::chooseWorkRoot(ram, base / "series", base / "cache", enorme).has_value());

    fs::remove_all(base);
}

static void testCleanupOrphanWorkDirs() {
    const auto base = fs::temp_directory_path() / "anidl_test_workdir";
    fs::remove_all(base);
    fs::create_directories(base);

    // Residui che devono sparire (workdir, publish temp, legacy).
    fs::create_directories(base / ".anidown_proc_123");
    fs::create_directories(base / ".anidown_proc_legacy");
    fs::create_directories(base / "anidown_proc_old");
    {
        std::ofstream f(base / ".anidown_proc_123" / "chunk.mp4");
        f << "x";
        std::ofstream p(base / ".anidown_publish_9.tmp");
        p << "x";
    }
    // File utente e nomi simili che NON devono essere toccati.
    {
        std::ofstream v(base / "Ep_01.mp4");
        v << "video";
        std::ofstream k(base / ".anidown_other");
        k << "keep";
    }

    Core::MediaProcessor::cleanupOrphanWorkDirs({base.string()});

    CHECK(!fs::exists(base / ".anidown_proc_123"));
    CHECK(!fs::exists(base / ".anidown_proc_legacy"));
    CHECK(!fs::exists(base / "anidown_proc_old"));
    CHECK(!fs::exists(base / ".anidown_publish_9.tmp"));
    CHECK(fs::exists(base / "Ep_01.mp4"));
    CHECK(fs::exists(base / ".anidown_other"));

    fs::remove_all(base);
}

int main() {
    testEstimateConversionBytes();
    testChooseWorkRoot();
    testCleanupOrphanWorkDirs();

    if (g_failures == 0) {
        std::cout << "test_workdir: tutti i test superati\n";
        return 0;
    }
    std::cerr << "test_workdir: " << g_failures << " fallimenti\n";
    return 1;
}
