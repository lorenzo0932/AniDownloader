#include "core/UpdateChecker.hpp"
#include "core/Logger.hpp"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <regex>

namespace Core {

void UpdateChecker::checkForUpdates(const std::string& currentVersion, Callback callback) {
    std::thread([currentVersion, callback]() {
        try {
            cpr::Response r = cpr::Get(
                cpr::Url{"https://api.github.com/repos/lorenzoAni/AniDownloader/releases/latest"},
                cpr::Header{{"Accept", "application/vnd.github.v3+json"}},
                cpr::Timeout{10000}
            );

            if (r.status_code != 200) {
                Logger::warn("Controllo aggiornamenti fallito (HTTP " + std::to_string(r.status_code) + ")");
                return;
            }

            auto json = nlohmann::json::parse(r.text);

            std::string tagName = json.value("tag_name", "");
            std::string htmlUrl = json.value("html_url", "");

            // Pulisci il tag: rimuovi "v" iniziale se presente
            std::string latestVersion = tagName;
            if (!latestVersion.empty() && latestVersion[0] == 'v') {
                latestVersion = latestVersion.substr(1);
            }

            if (latestVersion.empty()) {
                Logger::warn("Controllo aggiornamenti: tag_name vuoto nella risposta GitHub");
                return;
            }

            if (compareVersions(latestVersion, currentVersion) > 0) {
                Logger::info("Aggiornamento disponibile: " + latestVersion + " (corrente: " + currentVersion + ")");
                UpdateInfo info;
                info.updateAvailable = true;
                info.latestVersion = latestVersion;
                info.downloadUrl = htmlUrl;
                if (callback) callback(info);
            } else {
                Logger::info("Versione aggiornata: " + currentVersion);
            }
        } catch (const std::exception& e) {
            Logger::warn("Controllo aggiornamenti eccezione: " + std::string(e.what()));
        }
    }).detach();
}

int UpdateChecker::compareVersions(const std::string& a, const std::string& b) {
    auto parse = [](const std::string& v) -> std::vector<int> {
        std::vector<int> parts;
        std::regex numRegex(R"(\d+)");
        auto begin = std::sregex_iterator(v.begin(), v.end(), numRegex);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            parts.push_back(std::stoi((*it)[0].str()));
        }
        return parts;
    };

    std::vector<int> va = parse(a);
    std::vector<int> vb = parse(b);
    size_t maxLen = (std::max)(va.size(), vb.size());

    for (size_t i = 0; i < maxLen; ++i) {
        int na = (i < va.size()) ? va[i] : 0;
        int nb = (i < vb.size()) ? vb[i] : 0;
        if (na > nb) return 1;
        if (na < nb) return -1;
    }
    return 0;
}

}
