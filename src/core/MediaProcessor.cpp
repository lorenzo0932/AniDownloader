#ifndef _WIN32
    #include <sys/types.h>
    #include <unistd.h>
#else
    #include <process.h>
#endif

#include "core/MediaProcessor.hpp"
#include "scrapers/ScraperUtils.hpp"
#include <iostream>
#include <fstream>
#include <regex>
#include <thread>
#include <future>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <locale>
#include <algorithm>
#include <random>
#include <system_error>
#include <cmath> // Per std::abs nel calcolo delle tolleranze di durata

namespace fs = std::filesystem;

namespace Core {

std::mutex MediaProcessor::s_convMutex;
std::condition_variable MediaProcessor::s_convCv;
std::atomic<int> MediaProcessor::s_activeConversions{0};

MediaProcessor::MediaProcessor(ProgressCallback callback, std::atomic<bool>& stopSignal)
    : m_progressCallback(callback), m_stopSignal(stopSignal) {}

void MediaProcessor::notifyStop() {
    std::lock_guard<std::mutex> lock(s_convMutex);
    s_convCv.notify_all();
}

static std::string Q(const std::string& p) {
    std::string escaped = "'";
    for (char c : p) {
        if (c == '\'') escaped += "'\\''";
        else escaped += c;
    }
    escaped += "'";
    return escaped;
}

static std::string formatFloat(double value) {
    std::ostringstream oss;
    oss.imbue(std::locale::classic()); 
    oss << std::fixed << std::setprecision(6) << value;
    return oss.str();
}

static long long parseUs(const fs::path& p) {
    if (!fs::exists(p)) return 0;
    std::ifstream f(p); std::string line; long long t = 0;
    while (std::getline(f, line)) if (line.find("out_time_us=") == 0) try { t = std::stoll(line.substr(12)); } catch (...) {}
    return t;
}

void MediaProcessor::logError(const std::string& seriesName, const std::string& message) {
    try {
        std::ofstream logFile("mediaprocessor_errors.log", std::ios::app);
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        logFile << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") 
                << " - [CRITICAL] - " << seriesName << ": " << message << std::endl;
    } catch (...) {}
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
    if (!meminfo.is_open()) return 100.0;
    std::string line; long total = 1, available = 0;
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

std::string MediaProcessor::getVideoCodec(const std::string& filePath) {
    std::string cmd = "ffprobe -v error -select_streams v:0 -show_entries stream=codec_name -of default=noprint_wrappers=1:nokey=1 " + Q(filePath);
    std::string output;
    runCommand(cmd, [&](const std::string& line) { output += line; });
    
    // Rimuove eventuali ritorni a capo per normalizzare la stringa restituita
    output.erase(std::remove(output.begin(), output.end(), '\n'), output.end());
    output.erase(std::remove(output.begin(), output.end(), '\r'), output.end());
    return output;
}

bool MediaProcessor::isMediaFileHealthy(const std::string& filePath) {
    if (!fs::exists(filePath)) return false;
    
    // Un file video valido deve essere almeno di 1MB
    if (fs::file_size(filePath) < 1048576) return false;

    // Eseguiamo il demuxing veloce sul container per vedere se il file è troncato
    std::string verifyCmd = "ffmpeg -v error -xerror -i " + Q(filePath) + " -c copy -f null -";
    int status = runCommand(verifyCmd, nullptr);
    
    return (status == 0);
}

bool MediaProcessor::verifyIntegrity(const std::string& filePath, double expectedDuration) {
    double actualDuration = getVideoDuration(filePath);
    
    // Tolleranza del 2% con minimo di 10 secondi per prevenire falsi positivi
    double tolerance = std::max(10.0, expectedDuration * 0.02);

    if (std::abs(actualDuration - expectedDuration) > tolerance) {
        logError(filePath, "Verifica fallita: file troncato. Durata attesa: " + 
                 formatFloat(expectedDuration) + "s, Durata reale: " + formatFloat(actualDuration) + "s");
        return false;
    }

    // Demuxing rapido con gestione rigida degli errori (-xerror)
    std::string verifyCmd = "ffmpeg -v error -xerror -i " + Q(filePath) + " -c copy -f null -";
    int status = runCommand(verifyCmd, nullptr);
    
    if (status != 0) {
        logError(filePath, "Verifica fallita: rilevata corruzione del container o pacchetti invalidi (exit status " + std::to_string(status) + ")");
        return false;
    }

    return true;
}

ProcessResult MediaProcessor::processTask(const DownloadTask& task, const Series& series, const Config::ExecutionStrategy& strategy) {
    ProcessResult res;
    std::string expandedPath = ScraperUtils::expandTilde(series.path);
    std::string fullFile = (fs::path(expandedPath) / task.fileName).string();
    std::string aria2File = fullFile + ".aria2";

    bool needsDownload = true;
    bool needsConversion = strategy.convertToH265;

    // --- PRE-CHECK: RISOLUZIONE CRASH DI SISTEMA E COERENZA CODIFICA ---
    if (fs::exists(fullFile)) {
        m_progressCallback(series.name, "Verifica file esistente...");

        if (fs::exists(aria2File)) {
            // Caso 1: Trovato file .aria2 (download precedentemente interrotto)
            m_progressCallback(series.name, "Rilevato download incompleto, rimozione residui...");
            std::error_code ec;
            fs::remove(fullFile, ec);
            fs::remove(aria2File, ec);
        } 
        else if (!isMediaFileHealthy(fullFile)) {
            // Caso 2: Il file video esistente è corrotto o troncato da un arresto anomalo
            m_progressCallback(series.name, "Rilevato file corrotto, rimozione in corso...");
            std::error_code ec;
            fs::remove(fullFile, ec);
        } 
        else {
            // Caso 3: Il file è sano. Controlliamo se rispetta lo stato di codifica desiderato
            std::string codec = getVideoCodec(fullFile);
            
            if (strategy.convertToH265) {
                if (codec == "hevc" || codec == "h265") {
                    m_progressCallback(series.name, "✅ Già completato (H265)");
                    res.success = true;
                    return res;
                } else {
                    // Il file originale (H264) è sano, ma manca la conversione.
                    // Saltiamo il download e procediamo direttamente a convertire!
                    m_progressCallback(series.name, "Avvio conversione su file esistente...");
                    needsDownload = false;
                }
            } else {
                // Conversione non richiesta e file integro già presente
                m_progressCallback(series.name, "✅ Già completato");
                res.success = true;
                return res;
            }
        }
    }

    // --- FASE 1: DOWNLOAD CON ARIA2C ---
    auto startDl = std::chrono::steady_clock::now();
    if (needsDownload) {
        m_progressCallback(series.name, "Download...");
        std::string dlCmd = "aria2c -x 16 -s 16 --summary-interval=1 --allow-overwrite=true --dir=" + Q(expandedPath) + 
                            " -o " + Q(task.fileName) + " " + Q(task.videoUrl);
        
        static const std::regex dlRegex(R"raw(\((\d+)%\))raw");
        int status = runCommand(dlCmd, [&](const std::string& line) {
            std::smatch m; if (std::regex_search(line, m, dlRegex)) m_progressCallback(series.name, "DL " + m[1].str() + "%");
        });

        if (status != 0) {
            res.errorMessage = "Errore Aria2";
            logError(series.name, "Download fallito");
            return res;
        }
        res.downloadTime = std::chrono::duration<double>(std::chrono::steady_clock::now() - startDl).count();
    } else {
        res.downloadTime = 0.0;
    }

    // --- FASE 2: CONVERSIONE CON FFmpeg ---
    if (needsConversion) {
        m_progressCallback(series.name, "In coda...");
        {
            std::unique_lock<std::mutex> lock(s_convMutex);
            s_convCv.wait(lock, [&]{ return s_activeConversions < strategy.maxConcurrentTasks || m_stopSignal; });
            if (m_stopSignal) return res;
            s_activeConversions++;
        }

        bool ok = convertAndVerify(fullFile, series.name, strategy, res.conversionTime);

        {
            std::lock_guard<std::mutex> lock(s_convMutex);
            s_activeConversions--;
        }
        s_convCv.notify_one(); 

        if (!ok) {
            res.errorMessage = "Errore Conversione";
            return res;
        }
    } else {
        res.conversionTime = 0.0;
    }

    res.success = true;
    m_progressCallback(series.name, "✅ Completato");
    return res;
}

bool MediaProcessor::convertAndVerify(const std::string& inputPath, const std::string& seriesName, const Config::ExecutionStrategy& strategy, double& outTime) {
    const int MAX_RETRIES = 2;
    auto start = std::chrono::steady_clock::now();
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<unsigned long long> dis(1000, 9999);

    for (int attempt = 1; attempt <= MAX_RETRIES; ++attempt) {
        if (m_stopSignal) return false;
        
        m_progressCallback(seriesName, "Analisi...");
        
        bool isRamDisk = (getRamUsagePercent() < 55.0 && fs::exists("/dev/shm"));
        fs::path baseWorkDir = isRamDisk ? fs::path("/dev/shm") : fs::temp_directory_path();
        
        std::string uniqueId = std::to_string(std::hash<std::string>{}(inputPath)) + "_" + std::to_string(dis(gen));
        fs::path workDir = baseWorkDir / ("anidown_proc_" + uniqueId);
        
        fs::remove_all(workDir); 
        fs::create_directories(workDir);

        try {
            double duration = getVideoDuration(inputPath);
            if (duration <= 0) throw std::runtime_error("Impossibile leggere durata");

            #ifndef _WIN32
                std::string nicePrefix = "nice -n 19 ";
            #else
                std::string nicePrefix = "start \"\" /b /low ";
            #endif

            std::string baseArgs = "-c:v libx265 -crf 23 -preset veryfast -threads " + std::to_string(strategy.threadsPerFFmpeg) + 
                                   " -x265-params \"hist-scenecut=1\" -c:a copy";

            fs::path finalMergedInWork = workDir / "merged.mp4";

            if (strategy.chunksPerTask <= 1) {
                m_progressCallback(seriesName, "Encoding...");
                fs::path progP = workDir / "prog.txt";
                std::string cmd = nicePrefix + "ffmpeg -v error -y -i " + Q(inputPath) + " " + baseArgs + 
                                  " -progress " + Q(progP.string()) + " " + Q(finalMergedInWork.string()) + " > /dev/null 2>&1";
                
                auto f = std::async(std::launch::async, [cmd]() { return std::system(cmd.c_str()); });
                while (f.wait_for(std::chrono::milliseconds(500)) != std::future_status::ready) {
                    if (m_stopSignal) { fs::remove_all(workDir); return false; }
                    m_progressCallback(seriesName, "Conv " + std::to_string(std::min(99, (int)((parseUs(progP) * 100) / (duration * 1000000)))) + "%");
                }
                if (f.get() != 0) throw std::runtime_error("Errore FFmpeg");
            } 
            else {
                m_progressCallback(seriesName, "Splitting...");
                std::string split = "ffmpeg -v error -y -i " + Q(inputPath) + " -c copy -map 0 -f segment -segment_time " + 
                                    formatFloat(duration / strategy.chunksPerTask) + " -reset_timestamps 1 " + Q((workDir / "s%03d.mp4").string());
                if (std::system(split.c_str()) != 0) throw std::runtime_error("Errore split");

                std::vector<fs::path> parts;
                for (const auto& p : fs::directory_iterator(workDir)) 
                    if (p.path().filename().string().find("s") == 0) parts.push_back(p.path());
                std::sort(parts.begin(), parts.end());

                m_progressCallback(seriesName, "Encoding...");
                struct Job { fs::path out; fs::path prog; std::future<int> f; };
                std::vector<Job> jobs;
                for (const auto& p : parts) {
                    fs::path outP = p.string() + ".enc.mp4";
                    fs::path progP = p.string() + ".txt";
                    std::string cmd = nicePrefix + "ffmpeg -v error -y -i " + Q(p.string()) + " " + baseArgs + 
                                      " -progress " + Q(progP.string()) + " " + Q(outP.string()) + " > /dev/null 2>&1";
                    jobs.push_back({outP, progP, std::async(std::launch::async, [cmd]() { return std::system(cmd.c_str()); })});
                }

                while (true) {
                    if (m_stopSignal) { fs::remove_all(workDir); return false; }
                    bool allDone = true; long long cur = 0;
                    for (auto& j : jobs) {
                        if (j.f.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) allDone = false;
                        cur += parseUs(j.prog);
                    }
                    m_progressCallback(seriesName, "Conv " + std::to_string(std::min(99, (int)((cur * 100) / (duration * 1000000)))) + "%");
                    if (allDone) break; 
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }   
                for (auto& j : jobs) if (j.f.get() != 0) throw std::runtime_error("Errore in un chunk");

                m_progressCallback(seriesName, "Merging...");
                std::ofstream l(workDir / "list.txt");
                for (auto& j : jobs) l << "file '" << j.out.filename().string() << "'\n";
                l.close();
                std::string concat = "ffmpeg -v error -y -fflags +genpts -f concat -safe 0 -i " + Q((workDir / "list.txt").string()) + " -c copy " + Q(finalMergedInWork.string()) + " > /dev/null 2>&1";
                if (std::system(concat.c_str()) != 0) throw std::runtime_error("Errore merging");
            }

            m_progressCallback(seriesName, "Verifica...");
            if (verifyIntegrity(finalMergedInWork.string(), duration)) {
                
                m_progressCallback(seriesName, "Salvataggio...");
                
                std::error_code ec;
                fs::rename(finalMergedInWork, inputPath, ec);
                
                if (ec) {
                    fs::copy(finalMergedInWork, inputPath, fs::copy_options::overwrite_existing, ec);
                    if (ec) throw std::runtime_error("Impossibile copiare file sul disco finale: " + ec.message());
                }
                
                fs::remove_all(workDir);
                outTime = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                return true;
            } else throw std::runtime_error("Verifica file unito fallita");

        } catch (const std::exception& e) {
            logError(seriesName, "Tentativo " + std::to_string(attempt) + " fallito: " + std::string(e.what()));
            fs::remove_all(workDir);
            if (attempt == MAX_RETRIES) break;
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
    return false;
}

} // namespace Core