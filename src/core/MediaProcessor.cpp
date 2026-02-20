#include "core/MediaProcessor.hpp"
#include "core/scrapers/ScraperUtils.hpp"
#include <iostream>
#include <fstream>
#include <regex>
#include <thread>
#include <future>
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <unistd.h>

namespace fs = std::filesystem;

namespace Core {

MediaProcessor::MediaProcessor(ProgressCallback callback, std::atomic<bool>& stopSignal)
    : m_progressCallback(callback), m_stopSignal(stopSignal) {}

// Helper per il Quoting sicuro su Shell Linux (Gestisce spazi e virgolette doppie)
static std::string Q(const std::string& p) {
    std::string escaped = "'";
    for (char c : p) {
        if (c == '\'') escaped += "'\\''";
        else escaped += c;
    }
    escaped += "'";
    return escaped;
}

int MediaProcessor::runCommand(const std::string& cmd, std::function<void(const std::string&)> onLineRead) {
    std::string fullCmd = cmd + " 2>&1";
    FILE* pipe = popen(fullCmd.c_str(), "r");
    if (!pipe) return -1;

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        if (m_stopSignal) break;
        if (onLineRead) onLineRead(std::string(buffer));
    }
    return pclose(pipe);
}

double MediaProcessor::getRamUsagePercent() {
    std::ifstream meminfo("/proc/meminfo");
    std::string line; 
    long total = 1, available = 0;
    while (std::getline(meminfo, line)) {
        if (line.find("MemTotal:") == 0) std::sscanf(line.c_str(), "MemTotal: %ld", &total);
        if (line.find("MemAvailable:") == 0) std::sscanf(line.c_str(), "MemAvailable: %ld", &available);
    }
    return ((double)(total - available) / total) * 100.0;
}

double MediaProcessor::getVideoDuration(const std::string& filePath) {
    std::string cmd = "ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 " + Q(filePath);
    std::string output;
    runCommand(cmd, [&](const std::string& line) { output += line; });
    try { return std::stod(output); } catch (...) { return 0.0; }
}

static long long parseUs(const fs::path& p) {
    if (!fs::exists(p)) return 0;
    std::ifstream f(p); std::string line; long long t = 0;
    while (std::getline(f, line)) if (line.find("out_time_us=") == 0) try { t = std::stoll(line.substr(12)); } catch (...) {}
    return t;
}

ProcessResult MediaProcessor::processTask(const DownloadTask& task, const Series& series, bool convert, int chunks) {
    ProcessResult res;
    auto startDl = std::chrono::steady_clock::now();
    std::string expandedPath = ScraperUtils::expandTilde(series.path);

    m_progressCallback(series.name, "Download...");
    // Usiamo --dir e Q() per gestire spazi e virgolette doppie nel titolo
    std::string dlCmd = "aria2c -x 16 -s 16 --summary-interval=1 --allow-overwrite=true --dir=" + Q(expandedPath) + 
                        " -o " + Q(task.fileName) + " " + Q(task.videoUrl);
    
    static const std::regex dlRegex(R"raw(\((\d+)%\))raw");
    int status = runCommand(dlCmd, [&](const std::string& line) {
        std::smatch m; if (std::regex_search(line, m, dlRegex)) m_progressCallback(series.name, "DL " + m[1].str() + "%");
    });

    if (status != 0) { res.errorMessage = "Errore Aria2"; return res; }
    res.downloadTime = std::chrono::duration<double>(std::chrono::steady_clock::now() - startDl).count();

    if (convert) {
        std::string fullFile = (fs::path(expandedPath) / task.fileName).string();
        if (!convertAndVerify(fullFile, series.name, chunks, res.conversionTime)) {
            res.errorMessage = "Errore Conversione"; return res;
        }
    }

    res.success = true;
    m_progressCallback(series.name, "✅ Completato");
    return res;
}

bool MediaProcessor::convertAndVerify(const std::string& inputPath, const std::string& name, int chunks, double& outTime) {
    auto start = std::chrono::steady_clock::now();
    fs::path input(inputPath);
    
    fs::path workDir = (getRamUsagePercent() < 50.0 && fs::exists("/dev/shm")) ? fs::path("/dev/shm") : fs::temp_directory_path();
    workDir /= ("anidown_" + std::to_string(getpid()) + "_" + std::to_string(std::hash<std::string>{}(name)));
    fs::remove_all(workDir); fs::create_directories(workDir);

    double duration = getVideoDuration(inputPath);
    if (duration <= 0) return false;

    m_progressCallback(name, "Splitting...");
    std::string split = "ffmpeg -v error -y -i " + Q(inputPath) + " -c copy -f segment -segment_time " + std::to_string(duration/chunks) + " -reset_timestamps 1 " + Q((workDir / "part_%03d.mp4").string());
    if (std::system(split.c_str()) != 0) return false;

    std::vector<fs::path> parts;
    for (const auto& p : fs::directory_iterator(workDir)) if (p.path().extension() == ".mp4") parts.push_back(p.path());
    std::sort(parts.begin(), parts.end());

    m_progressCallback(name, "Encoding...");
    struct Job { fs::path out; fs::path prog; std::future<int> f; };
    std::vector<Job> jobs;
    for (const auto& p : parts) {
        fs::path outP = p.string() + ".enc.mp4";
        fs::path progP = p.string() + ".txt";
        std::string cmd = "ffmpeg -v error -y -i " + Q(p.string()) + " -c:v libx265 -crf 23 -preset veryfast -threads 4 -progress " + Q(progP.string()) + " " + Q(outP.string()) + " > /dev/null 2>&1";
        jobs.push_back({outP, progP, std::async(std::launch::async, [cmd]() { return std::system(cmd.c_str()); })});
    }

    while (true) {
        bool allDone = true; long long cur = 0;
        for (auto& j : jobs) {
            if (j.f.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) allDone = false;
            cur += parseUs(j.prog);
        }
        int pct = std::min(99, (int)((cur * 100) / (duration * 1000000)));
        m_progressCallback(name, "Conv " + std::to_string(pct) + "%");
        if (allDone) break; std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    m_progressCallback(name, "Merging...");
    std::ofstream l(workDir / "list.txt");
    for (auto& j : jobs) l << "file '" << j.out.filename().string() << "'\n";
    l.close();
    std::string concat = "ffmpeg -v error -y -f concat -safe 0 -i " + Q((workDir / "list.txt").string()) + " -c copy " + Q((workDir / "merged.mp4").string()) + " > /dev/null 2>&1";
    std::system(concat.c_str());

    fs::copy(workDir / "merged.mp4", inputPath, fs::copy_options::overwrite_existing);
    fs::remove_all(workDir);
    outTime = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return true;
}

}