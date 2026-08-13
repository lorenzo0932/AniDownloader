#pragma once

#include "core/SeriesRepository.hpp"
#include "core/ExecutionEngine.hpp"
#include "config/AppConfigManager.hpp"

#include <httplib.h>
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <queue>
#include <unordered_map>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace Web {

class WebServer {
public:
    WebServer(Config::AppConfigManager& configManager, int port);

    ~WebServer();

    bool start();
    void stop();

    int activePort() const { return m_port; }

    static std::string getLanIp();

private:
    Config::AppConfigManager& m_configManager;
    int m_port;

    std::filesystem::path m_configJsonPath;

    httplib::Server m_svr;
    Core::SeriesRepository m_seriesRepository;

    std::atomic<bool> m_downloadRunning{false};
    std::atomic<bool> m_stopSignal{false};
    std::thread m_downloadThread;

    // SSE event broadcast
    std::mutex m_sseMutex;
    std::unordered_map<uint64_t, std::queue<std::string>> m_sseQueues;
    uint64_t m_nextSseId = 0;
    std::atomic<uint64_t> m_sseCounter{0};

    void setupRoutes();

    nlohmann::json loadConfigJson();
    void saveConfigJson(const nlohmann::json& data);

    uint64_t registerSseClient();
    void unregisterSseClient(uint64_t id);
    void broadcastSseEvent(const std::string& eventJson);

    nlohmann::json errorJson(const std::string& message, int code = 400);
    nlohmann::json successJson(const nlohmann::json& data = nullptr);
    void sendJson(httplib::Response& res, const nlohmann::json& data, int status = 200);

    void runDownloads(const std::vector<Core::Series>& seriesList, bool burst);

    void serveEmbeddedFrontend();

    static std::string corsOrigin();
};

} // namespace Web
