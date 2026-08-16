// Test d'integrazione della pipeline media (download aria2 + conversione H265),
// completamente OFFLINE: il video sorgente è generato con ffmpeg e servito da
// un server HTTP locale in-process (cpp-httplib). Il piano prevedeva file://,
// ma aria2 1.37 non supporta il protocollo file:// (verificato in fase di
// implementazione): il fallback previsto è il server HTTP locale.
//
// Se aria2c/ffmpeg/ffprobe non sono installati il test viene saltato
// (skip con messaggio, ctest verde) — es. runner senza tool.
//
// Eseguire con: ctest --test-dir build -R test_media  (oppure ./build/test_media)
#include "config/AppConfigManager.hpp"
#include "core/MediaProbe.hpp"
#include "core/MediaProcessor.hpp"
#include "core/ProcessUtils.hpp"
#include "core/Series.hpp"
#include "httplib.h"
#include "scrapers/ScraperUtils.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

static int g_failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            ++g_failures;                                                                          \
            std::cerr << "FAIL: " << #cond << " (riga " << __LINE__ << ")\n";                      \
        }                                                                                          \
    } while (0)

static bool toolExists(const std::string& name) {
#ifdef _WIN32
    return std::system(("where " + name + " >NUL 2>&1").c_str()) == 0;
#else
    return std::system(("command -v " + name + " > /dev/null 2>&1").c_str()) == 0;
#endif
}

// Genera un video minuscolo valido (h264, 0.3s, moov all'inizio).
static bool generateTestVideo(const fs::path& outPath) {
    std::string cmd = "ffmpeg -y -v error -f lavfi -i color=c=red:s=64x64:d=0.3 "
                      "-c:v libx264 -pix_fmt yuv420p -movflags +faststart " +
                      Core::ScraperUtils::Q(outPath.string());
    std::atomic<bool> stop{false};
    return Core::ProcessUtils::runCommand(cmd, stop, nullptr) == 0 && fs::exists(outPath);
}

// Padda il file a >1MB (soglia file_size degli scan): il container mp4 con
// moov in testa tollera byte di coda (verificato empiricamente).
static void padToSize(const fs::path& p, std::uintmax_t minSize) {
    std::ofstream f(p, std::ios::binary | std::ios::app);
    std::string zeros(65536, '\0');
    while (fs::file_size(p) < minSize) {
        std::uintmax_t need = minSize - fs::file_size(p);
        std::size_t chunk =
            static_cast<std::size_t>((std::min)(need, (std::uintmax_t)zeros.size()));
        f.write(zeros.data(), static_cast<std::streamsize>(chunk));
    }
}

// Esegue un singolo task della pipeline su un MediaProcessor reale.
static Core::ProcessResult runTask(const fs::path& workDir, const std::string& url,
                                   const std::string& fileName, bool convert, int epNum) {
    Core::Series series;
    series.name = "TestSerie";
    series.path = workDir.string();
    Config::ExecutionStrategy strategy{2, 1, 4, convert, false};
    Core::DownloadTask task;
    task.shouldProcess = true;
    task.videoUrl = url;
    task.episodeNumber = epNum;
    task.fileName = fileName;
    std::atomic<bool> stop{false};
    Core::MediaProcessor mp([](const std::string&, const std::string&) {}, stop);
    return mp.processTask(task, series, strategy);
}

// --- Server HTTP con supporto Range (feature 11) ---
// set_content_provider: il body viene STREAMMATO sul socket in chunk, così il
// throttle rallenta davvero il trasferimento (con res.body il client riceveva
// tutto in un burst a fine handler e il .part non esisteva durante il
// download). Conta i byte effettivamente scritti e logga le richieste.
struct RangeServer {
    httplib::Server svr;
    std::thread thread;
    std::string url;
    fs::path file;
    std::atomic<uint64_t> bytesServed{0};
    std::atomic<int> requests{0};
    int chunkDelayMs = 0;

    RangeServer(fs::path f, int delayMs) : file(std::move(f)), chunkDelayMs(delayMs) {
        svr.Get("/file.mp4", [this](const httplib::Request&, httplib::Response& res) {
            auto size = static_cast<uint64_t>(fs::file_size(file));
            res.set_content_provider(
                size, "video/mp4",
                [this](uint64_t offset, uint64_t length, httplib::DataSink& sink) {
                    std::ifstream stream(file, std::ios::binary);
                    stream.seekg(static_cast<std::streamoff>(offset));
                    constexpr size_t CHUNK = 16384;
                    std::vector<char> buf(CHUNK);
                    uint64_t left = length;
                    while (left > 0) {
                        size_t n =
                            static_cast<size_t>((std::min)(left, static_cast<uint64_t>(CHUNK)));
                        stream.read(buf.data(), static_cast<std::streamsize>(n));
                        auto got = stream.gcount();
                        if (got <= 0)
                            return false;
                        if (!sink.write(buf.data(), static_cast<size_t>(got)))
                            return false;
                        left -= static_cast<uint64_t>(got);
                        bytesServed += static_cast<uint64_t>(got);
                        if (chunkDelayMs > 0)
                            std::this_thread::sleep_for(std::chrono::milliseconds(chunkDelayMs));
                    }
                    return true;
                });
            requests++;
        });
        int port = svr.bind_to_any_port("127.0.0.1");
        thread = std::thread([this]() { svr.listen_after_bind(); });
        url = "http://127.0.0.1:" + std::to_string(port) + "/file.mp4";
    }

    ~RangeServer() {
        svr.stop();
        if (thread.joinable())
            thread.join();
    }
};

// Uccide aria2 (una sola volta) per simulare SIGKILL sul processo figlio.
static void killAria2Once(const std::string& pattern [[maybe_unused]], std::atomic<bool>& fired) {
    if (fired.exchange(true))
        return;
#ifdef _WIN32
    const int rc = std::system("taskkill /F /IM aria2c.exe >NUL 2>&1");
    (void)rc;
#else
    const int rc = std::system(("pkill -9 -f \"" + pattern + "\" > /dev/null 2>&1").c_str());
    (void)rc;
#endif
}

// Copia i primi `frac` byte (0..1) del sorgente in un .part orfano.
static void writePartial(const fs::path& src, const fs::path& out, double frac) {
    std::ifstream in(src, std::ios::binary);
    std::ofstream outF(out, std::ios::binary);
    uint64_t n = static_cast<uint64_t>(fs::file_size(src) * frac);
    std::vector<char> buf(65536);
    while (n > 0) {
        auto c = (std::min)(n, static_cast<uint64_t>(buf.size()));
        in.read(buf.data(), static_cast<std::streamsize>(c));
        auto got = in.gcount();
        if (got <= 0)
            break;
        outF.write(buf.data(), got);
        n -= static_cast<uint64_t>(got);
    }
}

int main() {
    if (!toolExists("aria2c") || !toolExists("ffmpeg") || !toolExists("ffprobe")) {
        std::cout << "test_media: SKIP — servono aria2c, ffmpeg e ffprobe (non trovati)\n";
        return 0;
    }
    auto workDir = fs::temp_directory_path() / "anidl_test_media";
    fs::remove_all(workDir);
    fs::create_directories(workDir);

    // --- 1. Video sorgente valido, paddato a >1MB (8MB: finestra per il kill) ---
    fs::path src = workDir / "src.mp4";
    if (!generateTestVideo(src)) {
        std::cerr << "test_media: impossibile generare il video di test\n";
        fs::remove_all(workDir);
        return 1;
    }
    padToSize(src, 8000000);

    // --- 2. Server HTTP locale in-process (Range + conteggio byte) ---
    RangeServer srv(src, 4); // throttle 4ms/chunk: ~2s per giro (il kill a 1.4s avviene dopo il
                             // primo save del control file a 1s)
    std::string url = srv.url;

    // --- 3. Happy path con conversione H265 ---
    auto res1 = runTask(workDir, url, "Serie_Ep_01.mp4", true, 1);
    CHECK(res1.success);
    CHECK(fs::exists(workDir / "Serie_Ep_01.mp4"));
    if (res1.success) {
        std::atomic<bool> stop{false};
        std::string codec =
            Core::MediaProbe::getVideoCodec((workDir / "Serie_Ep_01.mp4").string(), stop);
        CHECK(codec == "hevc" || codec == "h265");
    }

    // --- 4. Senza conversione: solo download ---
    auto res2 = runTask(workDir, url, "Serie_Ep_02.mp4", false, 2);
    CHECK(res2.success);
    CHECK(fs::exists(workDir / "Serie_Ep_02.mp4"));
    if (res2.success) {
        std::atomic<bool> stop{false};
        std::string codec =
            Core::MediaProbe::getVideoCodec((workDir / "Serie_Ep_02.mp4").string(), stop);
        CHECK(codec == "h264");
    }

    // --- 5. Fail-fast: conversione locale con sorgente mancante (regressione fix) ---
    auto res3 = runTask(workDir, "", "Manca_Ep_03.mp4", true, 3);
    CHECK(!res3.success);
    CHECK(res3.errorMessage == "File sorgente mancante per la conversione locale");

    auto fileSize = fs::file_size(src);

    // --- 6. Scenario 1: SIGKILL ad aria2 (figlio muore, app viva) → il retry
    //         in-run riprende con --continue dallo stesso .part. Il kill
    //         avviene al ~70% del download (dopo il primo salvataggio del
    //         control file: --auto-save-interval=1) → il secondo giro serve
    //         solo i byte mancanti. ---
    {
        std::string partName = "Resume_Ep_01.mp4";
        fs::path partPath = workDir / (partName + ".part");
        uint64_t base = srv.bytesServed.load();
        std::atomic<bool> fired{false};
        std::atomic<uint64_t> bytesAtKill{0};
        std::thread killer([&]() {
            for (int i = 0; i < 600; ++i) {
                if (fs::exists(partPath) && srv.bytesServed - base >= fileSize * 7 / 10) {
                    bytesAtKill = srv.bytesServed.load();
                    killAria2Once(partName, fired);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
        auto res = runTask(workDir, url, partName, false, 1);
        killer.join();
        CHECK(res.success);
        CHECK(fs::exists(workDir / partName));
        uint64_t secondRound = srv.bytesServed - bytesAtKill;
        CHECK(secondRound > 0);
        CHECK(secondRound < fileSize); // resume: serviti solo i byte mancanti
        CHECK(!fs::exists(partPath));  // pubblicato, nessun residuo
        if (res.success) {
            std::atomic<bool> stop{false};
            CHECK(Core::MediaProbe::isMediaFileHealthy((workDir / partName).string(), stop));
        }
    }

    // --- 7. Scenario 2: SIGINT all'app → i parziali (.part + .aria2) restano
    //         (resume=true) e il run successivo riprende senza riscaricare ---
    {
        std::string partName = "Sigin_Ep_01.mp4";
        fs::path partPath = workDir / (partName + ".part");
        fs::path partAria = fs::path(partPath.string() + ".aria2");
        uint64_t base = srv.bytesServed.load();
        std::atomic<bool> stop{false};
        std::atomic<bool> fired{false};
        std::atomic<uint64_t> bytesAtStop{0};
        std::thread killer([&]() {
            for (int i = 0; i < 600; ++i) {
                if (fs::exists(partPath) && fs::exists(partAria) &&
                    srv.bytesServed - base >= fileSize * 7 / 10) {
                    bytesAtStop = srv.bytesServed.load();
                    killAria2Once(partName, fired);
                    stop = true; // SIGINT dell'app: stop segnalato alla pipeline
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
        Core::Series series;
        series.name = "TestSerie";
        series.path = workDir.string();
        Config::ExecutionStrategy strategy{2, 1, 4, false, false};
        Core::DownloadTask task;
        task.shouldProcess = true;
        task.videoUrl = url;
        task.episodeNumber = 1;
        task.fileName = partName;
        Core::MediaProcessor mp([](const std::string&, const std::string&) {}, stop);
        auto res = mp.processTask(task, series, strategy);
        killer.join();
        CHECK(!res.success);
        CHECK(fs::exists(partPath)); // parziali trattenuti per il resume
        CHECK(fs::exists(partAria));

        // Run successivo: il pre-check trova .part+.aria2 → resume
        auto resumeRes = runTask(workDir, url, partName, false, 1);
        CHECK(resumeRes.success);
        CHECK(fs::exists(workDir / partName));
        CHECK(!fs::exists(partPath));
        uint64_t secondRound = srv.bytesServed - bytesAtStop;
        CHECK(secondRound > 0);
        CHECK(secondRound < fileSize); // ripreso, non riscaricato da zero
    }

    // --- 8. Scenario 3: .part orfano senza .aria2 (crash nella finestra
    //         aria2→publish) → nessuna prova di corrispondenza: delete +
    //         redownload COMPLETO (byte serviti >= dimensione totale) ---
    {
        std::string partName = "Orfano_Ep_01.mp4";
        fs::path partPath = workDir / (partName + ".part");
        writePartial(src, partPath, 0.4);
        uint64_t before = srv.bytesServed;
        auto res = runTask(workDir, url, partName, false, 1);
        CHECK(res.success);
        CHECK(fs::exists(workDir / partName));
        CHECK(srv.bytesServed - before >= fileSize); // riscaricato tutto
        CHECK(!fs::exists(partPath));
    }

    // (RangeServer viene distrutto a fine main: stop() + join del listener)
    fs::remove_all(workDir);
    if (g_failures == 0) {
        std::cout << "test_media: tutti i test superati\n";
        return 0;
    }
    std::cerr << "test_media: " << g_failures << " fallimenti\n";
    return 1;
}
