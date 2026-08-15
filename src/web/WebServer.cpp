#include "web/WebServer.hpp"
#include "config/PathHelper.hpp"
#include "core/FileUtils.hpp"
#include "core/InstanceLock.hpp"
#include "core/LogUtils.hpp"
#include "core/Logger.hpp"
#include "core/MediaProcessor.hpp"
#include "core/PlanningService.hpp"
#include "core/SeriesUtils.hpp"
#include "scrapers/ScraperUtils.hpp"
#include "web/embedded_web.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#include <iphlpapi.h>
#include <windows.h>
#include <winsock2.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#elif defined(__APPLE__)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <mach-o/dyld.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Web {

    // Cap per la coda SSE per client: un client lento non deve far crescere la memoria
    // all'infinito.
    constexpr size_t kMaxSseQueueSize = 500;

    namespace {

        // Scrittura JSON atomica (tmp nella stessa dir + rename; fallback copy su
        // Windows dove rename fallisce con target esistente). Su eccezione rimuove il
        // tmp: il chiamante decide se loggare.
        void writeJsonAtomic(const std::filesystem::path& target, const nlohmann::json& data,
                             int indent) {
            std::filesystem::create_directories(target.parent_path());
            auto tmpPath = target;
            tmpPath += ".tmp";
            {
                std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
                if (!f.is_open())
                    throw std::runtime_error("Impossibile aprire " + tmpPath.string());
                f << data.dump(indent);
            }
            std::error_code ec;
            std::filesystem::rename(tmpPath, target, ec);
            if (ec) {
                std::filesystem::copy_file(tmpPath, target,
                                           std::filesystem::copy_options::overwrite_existing, ec);
                if (ec)
                    throw std::runtime_error("copy fallito: " + ec.message());
                std::filesystem::remove(tmpPath, ec);
            }
        }

    } // namespace

    WebServer::WebServer(Config::AppConfigManager& configManager, int port)
        : m_configManager(configManager), m_port(port),
          m_configJsonPath(Config::PathHelper::getConfigDir() / "config.json"),
          m_seriesRepository([&]() {
              std::string p = configManager.get<std::string>("json_file_path", "");
              return p.empty() ? Config::PathHelper::getSeriesJsonPath() : std::filesystem::path(p);
          }()) {
        // Ogni client SSE occupa un thread del pool per l'intera connessione:
        // pool dinamico proporzionale alla macchina (coerente con lo stile del
        // resto del codice, es. getExecutionStrategy), minimo garantito 16.
        // Il default httplib è max(8, hw-1): qui hw*2 dà headroom per gli SSE.
        auto hw = std::thread::hardware_concurrency();
        m_svr.new_task_queue = [hw] { return new httplib::ThreadPool((std::max)(16u, hw * 2)); };
    }

    WebServer::~WebServer() { stop(); }

    std::string WebServer::corsOrigin() { return "*"; }

    void WebServer::sendJson(httplib::Response& res, const nlohmann::json& data, int status) {
        res.status = status;
        res.set_header("Access-Control-Allow-Origin", corsOrigin());
        // Il campo "code" del body deve combaciare con lo status HTTP reale:
        // molti caller passavano lo status solo a sendJson (errorJson restava a 400).
        auto body = data;
        if (body.contains("code") && body["code"] != status) {
            body["code"] = status;
        }
        res.set_content(body.dump(), "application/json");
    }

    nlohmann::json WebServer::errorJson(const std::string& message, int code) {
        return {{"error", message}, {"code", code}};
    }

    nlohmann::json WebServer::successJson(const nlohmann::json& data) {
        if (data.is_null())
            return {{"success", true}};
        nlohmann::json result = {{"success", true}};
        if (data.is_object()) {
            for (auto& [k, v] : data.items())
                result[k] = v;
        } else {
            result["data"] = data;
        }
        return result;
    }

    // --- JSON file I/O ---

    nlohmann::json WebServer::loadConfigJson() {
        if (!std::filesystem::exists(m_configJsonPath))
            return nlohmann::json::object();
        try {
            std::ifstream f(m_configJsonPath);
            return nlohmann::json::parse(f);
        } catch (...) {
            return nlohmann::json::object();
        }
    }

    void WebServer::saveConfigJson(const nlohmann::json& data) {
        try {
            // Scrittura atomica (tmp + rename): evita config corrotto su crash.
            writeJsonAtomic(m_configJsonPath, data, 2);
        } catch (const std::exception& e) {
            Core::Logger::error("Impossibile salvare config web: " + std::string(e.what()));
        }
    }

    // --- SSE ---

    uint64_t WebServer::registerSseClient() {
        std::lock_guard<std::mutex> lock(m_sseMutex);
        auto id = m_nextSseId++;
        m_sseQueues[id] = {};
        m_sseCounter.store(m_sseQueues.size());
        return id;
    }

    void WebServer::unregisterSseClient(uint64_t id) {
        std::lock_guard<std::mutex> lock(m_sseMutex);
        m_sseQueues.erase(id);
        m_sseCounter.store(m_sseQueues.size());
    }

    void WebServer::broadcastSseEvent(const std::string& eventJson) {
        std::lock_guard<std::mutex> lock(m_sseMutex);
        for (auto& [id, queue] : m_sseQueues) {
            if (queue.size() >= kMaxSseQueueSize)
                queue.pop();
            queue.push(eventJson);
        }
    }

    // --- LAN IP detection ---

    std::string WebServer::getLanIp() {
#ifdef _WIN32
        DWORD size = 0;
        GetAdaptersInfo(nullptr, &size);
        auto buf = std::vector<uint8_t>(size);
        auto adapters = reinterpret_cast<PIP_ADAPTER_INFO>(buf.data());
        if (GetAdaptersInfo(adapters, &size) == NO_ERROR) {
            for (auto a = adapters; a; a = a->Next) {
                if (a->Type != MIB_IF_TYPE_LOOPBACK &&
                    a->IpAddressList.IpAddress.String[0] != '0') {
                    std::string ip = a->IpAddressList.IpAddress.String;
                    if (ip.find("169.254.") != 0 && ip.find("127.") != 0)
                        return ip;
                }
            }
        }
        return "0.0.0.0";
#else
        struct ifaddrs* ifaddr = nullptr;
        std::string result = "0.0.0.0";
        if (getifaddrs(&ifaddr) == -1)
            return result;

        for (auto* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
                continue;
            if (std::string(ifa->ifa_name) == "lo")
                continue;

            auto* addr = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
            std::string ipStr(ip);
            if (ipStr.find("169.254.") != 0 && ipStr.find("127.") != 0) {
                result = ipStr;
                break;
            }
        }
        freeifaddrs(ifaddr);
        return result;
#endif
    }

    // --- Routes ---

    void WebServer::setupRoutes() {
        // CORS preflight
        m_svr.Options(R"(.*)", [this](const httplib::Request&, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", corsOrigin());
            res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type");
            res.status = 204;
        });

        // Stesso ordine di registrazione del routing monolitico, raggruppato per
        // dominio (routes distinte: l'ordine tra gruppi non altera il matching).
        setupInfoRoutes();
        setupSeriesRoutes();
        setupBrowseRoutes();
        setupConfigRoutes();
        setupDownloadRoutes();

        // ---- EMBEDDED FRONTEND ----
        serveEmbeddedFrontend();
    }

    // ---- INFO ----
    void WebServer::setupInfoRoutes() {
        // ---- STATUS ----
        m_svr.Get("/api/status", [this](const httplib::Request&, httplib::Response& res) {
            sendJson(res, {{"version", ANIDOWNLOADER_VERSION},
                           {"sseClients", static_cast<uint64_t>(m_sseCounter.load())},
                           {"downloadRunning", m_downloadRunning.load()},
                           {"port", m_port}});
        });

        // ---- LOG ----
        m_svr.Get("/api/log", [this](const httplib::Request& req, httplib::Response& res) {
            int lines = 100;
            try {
                if (req.has_param("lines"))
                    lines = std::stoi(req.get_param_value("lines"));
            } catch (const std::exception&) {
                Core::Logger::warn("/api/log: parametro 'lines' non valido, uso default 100");
            }
            lines = std::clamp(lines, 10, 5000);

            // Il log è scritto su log_file_path della config: leggere lo stesso file,
            // non il default di PathHelper (divergevano se il path è personalizzato).
            auto logPath = m_configManager.get<std::string>(
                "log_file_path", Config::PathHelper::getLogFilePath().string());
            auto allLines = Core::getRecentLines(logPath, lines);
            nlohmann::json out = nlohmann::json::array();
            for (auto& l : allLines)
                out.push_back(l);
            sendJson(res, successJson({{"lines", out}}));
        });
    }

    // ---- SERIES ----
    void WebServer::setupSeriesRoutes() {
        // ---- SERIES ----
        m_svr.Get("/api/series", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                auto vec = m_seriesRepository.loadSeriesData();
                nlohmann::json data = nlohmann::json::array();
                for (size_t i = 0; i < vec.size(); ++i) {
                    nlohmann::json entry = vec[i];
                    auto path = entry.value("path", "");
                    entry["local_episode_count"] = path.empty() ? 0 : Core::countVideoFiles(path);
                    entry["_file_index"] = static_cast<uint64_t>(i);
                    data.push_back(std::move(entry));
                }
                std::string sortField = req.has_param("sort") ? req.get_param_value("sort") : "";
                if (!sortField.empty()) {
                    bool desc = req.has_param("dir") && req.get_param_value("dir") == "desc";
                    Core::sortSeries(data, sortField, desc);
                }
                sendJson(res, successJson({{"series", data}}));
            } catch (const std::exception& e) {
                sendJson(res, errorJson("Failed to load series: " + std::string(e.what())), 500);
            }
        });

        m_svr.Post("/api/series", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                auto body = nlohmann::json::parse(req.body);
                auto series = body.get<Core::Series>();
                auto vec = m_seriesRepository.loadSeriesData();
                vec.push_back(series);
                m_seriesRepository.saveSeriesData(vec);
                sendJson(res, successJson({{"index", vec.size() - 1}}));
            } catch (const std::exception& e) {
                sendJson(res, errorJson("Invalid series data: " + std::string(e.what())), 400);
            }
        });

        m_svr.Put(R"(/api/series/(\d+))",
                  [this](const httplib::Request& req, httplib::Response& res) {
                      try {
                          auto idx = std::stoi(req.matches[1]);
                          auto body = nlohmann::json::parse(req.body);
                          auto vec = m_seriesRepository.loadSeriesData();
                          if (idx < 0 || static_cast<size_t>(idx) >= vec.size()) {
                              sendJson(res, errorJson("Index out of range"), 404);
                              return;
                          }
                          vec[idx] = body.get<Core::Series>();
                          m_seriesRepository.saveSeriesData(vec);
                          sendJson(res, successJson());
                      } catch (const std::exception& e) {
                          sendJson(res, errorJson("Invalid data: " + std::string(e.what())), 400);
                      }
                  });

        m_svr.Delete(
            R"(/api/series/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
                try {
                    auto idx = std::stoi(req.matches[1]);
                    auto vec = m_seriesRepository.loadSeriesData();
                    if (idx < 0 || static_cast<size_t>(idx) >= vec.size()) {
                        sendJson(res, errorJson("Index out of range"), 404);
                        return;
                    }
                    vec.erase(vec.begin() + idx);
                    m_seriesRepository.saveSeriesData(vec);
                    sendJson(res, successJson());
                } catch (const std::exception& e) {
                    sendJson(res, errorJson("Invalid request: " + std::string(e.what())), 400);
                }
            });

        // ---- FETCH SERIES NAME FROM URL ----
        m_svr.Post("/api/series/fetch-name",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       try {
                           auto body = nlohmann::json::parse(req.body);
                           std::string url = body.value("url", "");
                           if (url.empty()) {
                               sendJson(res, errorJson("Missing url"), 400);
                               return;
                           }
                           std::string name = Core::ScraperUtils::fetchSeriesNameFromUrl(url);
                           sendJson(res, successJson({{"name", name}}));
                       } catch (const std::exception& e) {
                           sendJson(res, errorJson(std::string("Fetch failed: ") + e.what()), 500);
                       }
                   });

        // ---- DESCRIPTION (tvshow.nfo) ----
        m_svr.Get(R"(/api/series/(\d+)/description)",
                  [this](const httplib::Request& req, httplib::Response& res) {
                      try {
                          auto idx = std::stoi(req.matches[1]);
                          auto vec = m_seriesRepository.loadSeriesData();
                          if (idx < 0 || static_cast<size_t>(idx) >= vec.size()) {
                              sendJson(res, errorJson("Index out of range"), 404);
                              return;
                          }
                          auto desc = Core::readNfoDescription(vec[idx].path);
                          sendJson(res, successJson({{"description", desc}}));
                      } catch (...) {
                          sendJson(res, successJson({{"description", ""}}));
                      }
                  });

        // ---- POSTER (legacy, index-based) ----
        m_svr.Get(R"(/api/series/(\d+)/poster)", [this](const httplib::Request& req,
                                                        httplib::Response& res) {
            try {
                auto idx = std::stoi(req.matches[1]);
                auto vec = m_seriesRepository.loadSeriesData();
                if (idx < 0 || static_cast<size_t>(idx) >= vec.size()) {
                    sendJson(res, errorJson("Index out of range"), 404);
                    return;
                }
                if (vec[idx].path.empty())
                    throw std::runtime_error("no path");

                auto posterPath = Core::findPosterPath(vec[idx].path);
                if (!posterPath.empty()) {
                    std::ifstream f(posterPath, std::ios::binary | std::ios::ate);
                    auto size = f.tellg();
                    f.seekg(0);
                    std::string content(size, '\0');
                    f.read(content.data(), size);
                    res.set_header("Access-Control-Allow-Origin", corsOrigin());
                    res.set_content(content, "image/jpeg");
                } else {
                    std::string svg =
                        R"(<svg xmlns="http://www.w3.org/2000/svg" width="220" height="320">)"
                        R"(<rect width="220" height="320" fill="#2a2a2a" rx="8"/>)"
                        R"(<text x="110" y="160" fill="#666" font-family="sans-serif" font-size="14")"
                        R"( text-anchor="middle" dominant-baseline="middle">Locandina</text>)"
                        R"(<text x="110" y="180" fill="#666" font-family="sans-serif" font-size="12")"
                        R"( text-anchor="middle" dominant-baseline="middle">non trovata</text></svg>)";
                    res.set_header("Access-Control-Allow-Origin", corsOrigin());
                    res.set_content(svg, "image/svg+xml");
                }
            } catch (...) {
                sendJson(res, errorJson("Poster not available"), 404);
            }
        });
    }

    // ---- BROWSE ----
    void WebServer::setupBrowseRoutes() {
        // ---- BROWSE ----
        m_svr.Get("/api/browse", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                std::string path = req.has_param("path") ? req.get_param_value("path") : "/";
                auto dirs = Core::listDirectories(path);
                nlohmann::json entries = nlohmann::json::array();
                for (auto& de : dirs) {
                    nlohmann::json obj;
                    obj["name"] = de.name;
                    obj["path"] = de.path;
                    obj["mtime"] = de.mtime;
                    entries.push_back(obj);
                }
                sendJson(res, successJson({{"entries", entries}}));
            } catch (const std::exception& e) {
                sendJson(res, errorJson("Browse error: " + std::string(e.what())), 400);
            }
        });

        // ---- NATIVE DIRECTORY PICKER ----
        m_svr.Post("/api/browse/pick", [this](const httplib::Request& req, httplib::Response& res) {
            std::string path;
            std::string currentPath;
            try {
                auto body = nlohmann::json::parse(req.body);
                auto it = body.find("current_path");
                if (it != body.end() && it->is_string()) {
                    std::string candidate = it->get<std::string>();
                    if (!candidate.empty() && std::filesystem::is_directory(candidate))
                        currentPath = candidate;
                }
            } catch (const std::exception& e) {
                Core::Logger::warn("/api/browse/pick: body non valido ignorato: " +
                                   std::string(e.what()));
            }
#ifdef _WIN32
            // Windows: not implemented via cross-compilation, rely on frontend fallback
            sendJson(res, errorJson("Not available on this platform"), 501);
            return;
#elif defined(__APPLE__)
        std::string cmd = "osascript -e 'POSIX path of (choose folder";
        if (!currentPath.empty())
            cmd += " default location \"" + currentPath + "\"";
        cmd += ")' 2>/dev/null";
        FILE* fp = popen(cmd.c_str(), "r");
        if (!fp) {
            sendJson(res, errorJson("No dialog tool available"), 501);
            return;
        }
        char buf[4096] = {};
        if (fgets(buf, sizeof(buf), fp)) {
            path = buf;
            path.erase(std::find_if(path.rbegin(), path.rend(),
                [](int c) { return c != '\n' && c != '\r'; }).base(), path.end());
        }
        int status = pclose(fp);
        if (path.empty() || status != 0) {
            sendJson(res, errorJson("No directory selected"), 400);
            return;
        }
        sendJson(res, successJson({{"path", path}}));
#else
        std::string cmd;
        if (system("which zenity >/dev/null 2>&1") == 0) {
            cmd = "zenity --file-selection --directory";
            if (!currentPath.empty())
                cmd += " --filename=\"" + currentPath + "/\"";
            cmd += " 2>/dev/null";
        } else if (system("which kdialog >/dev/null 2>&1") == 0) {
            cmd = "kdialog --getexistingdirectory";
            if (!currentPath.empty())
                cmd += " \"" + currentPath + "\"";
            else
                cmd += " .";
            cmd += " 2>/dev/null";
        } else {
            sendJson(res, errorJson("No dialog tool available (install zenity or kdialog)"), 501);
            return;
        }
        FILE* fp = popen(cmd.c_str(), "r");
        if (!fp) {
            sendJson(res, errorJson("No dialog tool available (install zenity or kdialog)"), 501);
            return;
        }
        char buf[4096] = {};
        if (fgets(buf, sizeof(buf), fp)) {
            path = buf;
            path.erase(std::find_if(path.rbegin(), path.rend(),
                [](int c) { return c != '\n' && c != '\r'; }).base(), path.end());
        }
        int status = pclose(fp);
        if (path.empty() || status != 0) {
            sendJson(res, errorJson("No directory selected"), 400);
            return;
        }
        sendJson(res, successJson({{"path", path}}));
#endif
        });

        // ---- POSTER (path-based, stabile con ordinamento) ----
        m_svr.Get("/api/poster", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                if (!req.has_param("path")) {
                    sendJson(res, errorJson("Missing path"), 400);
                    return;
                }
                auto pathStr = req.get_param_value("path");
                auto posterPath = Core::findPosterPath(pathStr);
                if (!posterPath.empty()) {
                    std::ifstream f(posterPath, std::ios::binary | std::ios::ate);
                    auto size = f.tellg();
                    f.seekg(0);
                    std::string content(size, '\0');
                    f.read(content.data(), size);
                    res.set_header("Access-Control-Allow-Origin", corsOrigin());
                    res.set_content(content, "image/jpeg");
                } else {
                    std::string svg =
                        R"(<svg xmlns="http://www.w3.org/2000/svg" width="220" height="320">)"
                        R"(<rect width="220" height="320" fill="#2a2a2a" rx="8"/>)"
                        R"(<text x="110" y="160" fill="#666" font-family="sans-serif" font-size="14")"
                        R"( text-anchor="middle" dominant-baseline="middle">Locandina</text>)"
                        R"(<text x="110" y="180" fill="#666" font-family="sans-serif" font-size="12")"
                        R"( text-anchor="middle" dominant-baseline="middle">non trovata</text></svg>)";
                    res.set_header("Access-Control-Allow-Origin", corsOrigin());
                    res.set_content(svg, "image/svg+xml");
                }
            } catch (...) {
                sendJson(res, errorJson("Poster not available"), 404);
            }
        });

        // ---- DESCRIPTION (path-based, stabile con ordinamento) ----
        m_svr.Get("/api/description", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                if (!req.has_param("path")) {
                    sendJson(res, errorJson("Missing path"), 400);
                    return;
                }
                auto pathStr = req.get_param_value("path");
                auto desc = Core::readNfoDescription(pathStr);
                sendJson(res, successJson({{"description", desc}}));
            } catch (...) {
                sendJson(res, successJson({{"description", ""}}));
            }
        });
    }

    // ---- CONFIG ----
    void WebServer::setupConfigRoutes() {
        // ---- CONFIG ----
        m_svr.Get("/api/config", [this](const httplib::Request&, httplib::Response& res) {
            try {
                auto data = loadConfigJson();
                sendJson(res, successJson({{"config", data}}));
            } catch (const std::exception& e) {
                sendJson(res, errorJson("Failed to load config"), 500);
            }
        });

        m_svr.Put("/api/config", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                auto body = nlohmann::json::parse(req.body);
                auto existing = loadConfigJson();
                for (auto& [key, val] : body.items())
                    existing[key] = val;
                saveConfigJson(existing);
                sendJson(res, successJson());
            } catch (const std::exception& e) {
                sendJson(res, errorJson("Invalid config data"), 400);
            }
        });
    }

    // ---- DOWNLOAD ----
    void WebServer::setupDownloadRoutes() {
        // ---- DOWNLOAD ----
        m_svr.Post("/api/download/start", [this](const httplib::Request& req,
                                                 httplib::Response& res) {
            // Claim atomico: evita la race check-then-act (due POST ravvicinate avviavano
            // due download in parallelo, con il secondo bloccato sul join del primo).
            if (m_downloadRunning.exchange(true)) {
                sendJson(res, errorJson("Download already in progress"), 409);
                return;
            }
            try {
                bool burst = true;
                if (!req.body.empty()) {
                    auto body = nlohmann::json::parse(req.body);
                    burst = body.value("burst", true);
                }
                auto& seriesList = m_seriesRepository.loadSeriesData();
                if (seriesList.empty()) {
                    m_downloadRunning.store(false);
                    sendJson(res, errorJson("No series configured"), 400);
                    return;
                }
                sendJson(res, successJson({{"message", "Download started"}}));
                runDownloads(seriesList, burst);
            } catch (const std::exception& e) {
                m_downloadRunning.store(false);
                sendJson(res, errorJson("Failed to start download: " + std::string(e.what())), 500);
            }
        });

        m_svr.Post("/api/download/stop", [this](const httplib::Request&, httplib::Response& res) {
            if (m_downloadRunning.load()) {
                m_stopSignal.store(true);
                Core::MediaProcessor::notifyStop();

#ifndef _WIN32
                {
                    pid_t child = fork();
                    if (child == 0) {
                        pid_t ppid = getppid();
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%d", ppid);
                        execl("/bin/sh", "sh", "-c",
                              ("pids=$(pgrep -P " + std::string(buf) +
                               " 2>/dev/null); "
                               "for pid in $pids; do pkill -9 -P $pid 2>/dev/null; "
                               "kill -9 $pid 2>/dev/null; done")
                                  .c_str(),
                              nullptr);
                        _exit(127);
                    }
                    if (child > 0)
                        waitpid(child, nullptr, WNOHANG);
                }
#else
            DWORD myPid = GetCurrentProcessId();
            std::string killCmd =
                std::format("taskkill /F /FI \"PPID eq {}\" /T >nul 2>&1", myPid);
            std::system(killCmd.c_str());
#endif

                Core::Logger::info("Download stop requested");
                sendJson(res, successJson({{"message", "Stop signal sent"}}));
            } else {
                sendJson(res, successJson({{"message", "No download in progress"}}));
            }
        });

        m_svr.Get("/api/download/status", [this](const httplib::Request&, httplib::Response& res) {
            sendJson(res, successJson({{"running", m_downloadRunning.load()}}));
        });

        // ---- SSE ----
        m_svr.Get("/api/download/events", [this](const httplib::Request&, httplib::Response& res) {
            auto clientId = registerSseClient();

            res.set_header("Cache-Control", "no-cache");
            res.set_header("Connection", "keep-alive");
            res.set_header("Access-Control-Allow-Origin", corsOrigin());

            auto closed = std::make_shared<std::atomic<bool>>(false);

            res.set_chunked_content_provider(
                "text/event-stream",
                [this, clientId, closed](size_t, httplib::DataSink& sink) -> bool {
                    if (closed->load())
                        return false;

                    std::vector<std::string> events;
                    {
                        std::lock_guard<std::mutex> lock(m_sseMutex);
                        auto it = m_sseQueues.find(clientId);
                        if (it != m_sseQueues.end()) {
                            while (!it->second.empty()) {
                                events.push_back(std::move(it->second.front()));
                                it->second.pop();
                            }
                        }
                    }

                    for (auto& ev : events) {
                        sink.os << "data: " << ev << "\n\n";
                    }
                    if (!events.empty())
                        sink.os << std::flush;

                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    return true;
                },
                [this, clientId, closed](bool) {
                    closed->store(true);
                    unregisterSseClient(clientId);
                });
        });
    }

    void WebServer::serveEmbeddedFrontend() {
        const auto& files = getEmbeddedFiles();

        // Root path -> index.html
        {
            auto it = files.find("/index.html");
            if (it != files.end()) {
                const auto& f = it->second;
                m_svr.Get("/", [ptr = f.data, sz = f.size](const httplib::Request&,
                                                           httplib::Response& res) {
                    res.set_content(std::string(reinterpret_cast<const char*>(ptr), sz),
                                    "text/html");
                    res.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
                    res.set_header("Pragma", "no-cache");
                });
            }
        }

        // Register explicit GET routes for each embedded file (no regex, avoids std::regex
        // thread-safety issues)
        for (const auto& [path, file] : files) {
            if (path == "/index.html")
                continue;
            // Gli asset sotto /assets/ hanno nomi hashati da Vite (nuova build =
            // nuovi nomi): cache lunga immutable. index.html e il resto restano
            // no-cache così la SPA rilegge sempre l'HTML e poi i nuovi asset.
            bool immutable = path.rfind("/assets/", 0) == 0;
            m_svr.Get(std::string(path), [ptr = file.data, sz = file.size,
                                          mt = std::string(file.mime_type), immutable](
                                             const httplib::Request&, httplib::Response& res) {
                res.set_content(std::string(reinterpret_cast<const char*>(ptr), sz), mt);
                if (immutable) {
                    res.set_header("Cache-Control", "public, max-age=31536000, immutable");
                } else {
                    res.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
                    res.set_header("Pragma", "no-cache");
                }
            });
        }

        // SPA fallback: for non-API 404s, serve index.html
        m_svr.set_error_handler([&files](const httplib::Request& req, httplib::Response& res) {
            if (res.status == 404 && req.path.find("/api/") != 0) {
                auto it = files.find("/index.html");
                if (it != files.end()) {
                    const auto& file = it->second;
                    res.set_content(
                        std::string(reinterpret_cast<const char*>(file.data), file.size),
                        "text/html");
                    res.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
                    res.set_header("Pragma", "no-cache");
                    res.status = 200;
                }
            }
        });

        Core::Logger::info(std::format("Frontend: embedded ({} files, {} bytes)", files.size(),
                                       files.find("/index.html")->second.size));
    }

    bool WebServer::start() {
        setupRoutes();

        int actualPort = m_port;
        int maxAttempts = 10;

        for (int i = 0; i < maxAttempts; ++i) {
            if (m_svr.bind_to_port("0.0.0.0", actualPort)) {
                m_port = actualPort;
                break;
            }
            actualPort = m_port + i + 1;
            if (i == maxAttempts - 1) {
                Core::Logger::error(
                    std::format("No available port after {} attempts", maxAttempts));
                return false;
            }
        }

        auto lanIp = getLanIp();
        Core::Logger::info(std::format("Web server listening on port {}", m_port));
        Core::Logger::info(std::format("Local:  http://localhost:{}", m_port));
        if (lanIp != "0.0.0.0" && lanIp != "127.0.0.1") {
            Core::Logger::info(std::format("LAN:    http://{}:{}", lanIp, m_port));
        }

        m_svr.listen_after_bind();
        return true;
    }

    void WebServer::stop() {
        m_stopSignal.store(true);
        if (m_downloadThread.joinable()) {
            m_downloadThread.join();
        }
        m_svr.stop();
    }

    void WebServer::runDownloads(const std::vector<Core::Series>& seriesList, bool burst) {
        if (m_downloadThread.joinable()) {
            m_downloadThread.join();
        }

        m_stopSignal.store(false);
        m_downloadRunning.store(true);

        m_downloadThread = std::thread([this, seriesList, burst]() {
            // Feature 11: lock transazionale (planning → download → save).
            // Un'altra istanza (CLI o altro demone) attiva → rifiuto chiaro.
            std::filesystem::path execLockPath =
                Config::PathHelper::getConfigDir() / "exec.lock";
            Core::InstanceLock execLock(execLockPath.string());
            if (!execLock.acquired()) {
                nlohmann::json ev = {{"type", "overall"},
                                     {"status", "Già in corso: un'altra esecuzione è attiva"}};
                broadcastSseEvent(ev.dump());
                m_downloadRunning.store(false);
                return;
            }

            m_configManager.reloadConfig();
            Core::ExecutionEngine engine(m_configManager);

            std::mutex mapMutex;
            std::map<std::string, int> maxEpisodes;

            engine.run(
                seriesList, burst, m_stopSignal,
                [this](const std::string& name, int ep, const std::string& msg) {
                    nlohmann::json ev = {{"type", "progress"}, {"series", name}, {"message", msg}};
                    if (ep > 0)
                        ev["episode"] = ep;
                    broadcastSseEvent(ev.dump());
                },
                [this](const std::string& status) {
                    nlohmann::json ev = {{"type", "overall"}, {"status", status}};
                    broadcastSseEvent(ev.dump());
                },
                [this, &mapMutex, &maxEpisodes](const Core::TaskReport& report) {
                    if (report.success && report.episodeNumber > 0) {
                        std::lock_guard<std::mutex> lock(mapMutex);
                        maxEpisodes[report.name] =
                            (std::max)(maxEpisodes[report.name], report.episodeNumber);
                    }
                    nlohmann::json ev = {{"type", "finished"},
                                         {"series", report.name},
                                         {"episode", report.episodeNumber},
                                         {"success", report.success},
                                         {"dlTime", report.dlTime},
                                         {"convTime", report.convTime},
                                         {"error", report.error}};
                    broadcastSseEvent(ev.dump());
                },
                [this](const std::string& name, const std::string& reason) {
                    nlohmann::json ev = {{"type", "skipped"}, {"series", name}, {"reason", reason}};
                    broadcastSseEvent(ev.dump());
                },
                [this]() {
                    nlohmann::json ev = {{"type", "phase"}, {"phase", "processing"}};
                    broadcastSseEvent(ev.dump());
                });

            if (!maxEpisodes.empty()) {
                auto now = std::chrono::system_clock::now();
                auto tt = std::chrono::system_clock::to_time_t(now);
                auto tm = *std::gmtime(&tt);
                char buf[24] = {};
                std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);

                m_seriesRepository.applyDownloadedEpisodes(maxEpisodes, buf);
            }

            m_downloadRunning.store(false);
            m_stopSignal.store(false);

            nlohmann::json ev = {{"type", "done"}, {"running", false}};
            broadcastSseEvent(ev.dump());

            Core::Logger::info("Download execution completed");
        });
    }

} // namespace Web
