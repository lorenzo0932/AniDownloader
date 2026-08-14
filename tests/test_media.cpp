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
#include "core/MediaProcessor.hpp"
#include "core/MediaProbe.hpp"
#include "core/ProcessUtils.hpp"
#include "core/Series.hpp"
#include "config/AppConfigManager.hpp"
#include "scrapers/ScraperUtils.hpp"
#include "httplib.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <algorithm>

namespace fs = std::filesystem;

static int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            ++g_failures;                                               \
            std::cerr << "FAIL: " << #cond << " (riga " << __LINE__ << ")\n"; \
        }                                                               \
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
                      "-c:v libx264 -pix_fmt yuv420p -movflags +faststart "
                      + Core::ScraperUtils::Q(outPath.string());
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
        std::size_t chunk = static_cast<std::size_t>((std::min)(need, (std::uintmax_t)zeros.size()));
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

int main() {
    if (!toolExists("aria2c") || !toolExists("ffmpeg") || !toolExists("ffprobe")) {
        std::cout << "test_media: SKIP — servono aria2c, ffmpeg e ffprobe (non trovati)\n";
        return 0;
    }

    auto workDir = fs::temp_directory_path() / "anidl_test_media";
    fs::remove_all(workDir);
    fs::create_directories(workDir);

    // --- 1. Video sorgente valido, paddato a >1MB ---
    fs::path src = workDir / "src.mp4";
    if (!generateTestVideo(src)) {
        std::cerr << "test_media: impossibile generare il video di test\n";
        fs::remove_all(workDir);
        return 1;
    }
    padToSize(src, 1100000);

    // --- 2. Server HTTP locale in-process ---
    httplib::Server svr;
    svr.Get("/file.mp4", [&src](const httplib::Request&, httplib::Response& res) {
        std::ifstream f(src, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(f)), {});
        res.set_content(content, "video/mp4");
    });
    int port = svr.bind_to_any_port("127.0.0.1");
    std::thread serverThread([&svr]() { svr.listen_after_bind(); });
    std::string url = "http://127.0.0.1:" + std::to_string(port) + "/file.mp4";

    // --- 3. Happy path con conversione H265 ---
    auto res1 = runTask(workDir, url, "Serie_Ep_01.mp4", true, 1);
    CHECK(res1.success);
    CHECK(fs::exists(workDir / "Serie_Ep_01.mp4"));
    if (res1.success) {
        std::atomic<bool> stop{false};
        std::string codec = Core::MediaProbe::getVideoCodec((workDir / "Serie_Ep_01.mp4").string(), stop);
        CHECK(codec == "hevc" || codec == "h265");
    }

    // --- 4. Senza conversione: solo download ---
    auto res2 = runTask(workDir, url, "Serie_Ep_02.mp4", false, 2);
    CHECK(res2.success);
    CHECK(fs::exists(workDir / "Serie_Ep_02.mp4"));
    if (res2.success) {
        std::atomic<bool> stop{false};
        std::string codec = Core::MediaProbe::getVideoCodec((workDir / "Serie_Ep_02.mp4").string(), stop);
        CHECK(codec == "h264");
    }

    // --- 5. Fail-fast: conversione locale con sorgente mancante (regressione fix) ---
    auto res3 = runTask(workDir, "", "Manca_Ep_03.mp4", true, 3);
    CHECK(!res3.success);
    CHECK(res3.errorMessage == "File sorgente mancante per la conversione locale");

    svr.stop();
    serverThread.join();
    fs::remove_all(workDir);

    if (g_failures == 0) {
        std::cout << "test_media: tutti i test superati\n";
        return 0;
    }
    std::cerr << "test_media: " << g_failures << " fallimenti\n";
    return 1;
}
