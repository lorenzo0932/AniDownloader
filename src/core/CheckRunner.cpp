#include "core/CheckRunner.hpp"

#include "config/AppConfigManager.hpp"
#include "config/PathHelper.hpp"
#include "core/ExecutionEngine.hpp"
#include "core/InstanceLock.hpp"
#include "core/Logger.hpp"
#include "core/SeriesRepository.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <map>

namespace Core {

    CheckOutcome CheckRunner::runOnce(
        Config::AppConfigManager& config, const std::string& jsonPath, bool burst,
        std::atomic<bool>& stop,
        std::function<void(const std::string&, int, const std::string&)> onProgress,
        std::function<void(const std::string&)> onOverall) {
        CheckOutcome esito;
        SeriesRepository repo(jsonPath, SeriesRepository::dbPathFor(jsonPath));
        auto serie = repo.loadSeriesData();
        if (serie.empty()) {
            esito.salto = CheckOutcome::Salto::Vuoto;
            esito.nota = "nessuna serie configurata";
            return esito;
        }

        // Lock non bloccante: se un altro giro è in corso (CLI manuale o
        // demone), si rimanda senza accavallarsi — mai due giri insieme.
        std::filesystem::path execLockPath = Config::PathHelper::getConfigDir() / "exec.lock";
        Core::InstanceLock execLock(execLockPath.string());
        if (!execLock.acquired()) {
            esito.salto = CheckOutcome::Salto::Occupato;
            esito.nota = "giro già in corso (lock " + execLockPath.string() + ")";
            return esito;
        }

        ExecutionEngine engine(config);
        engine.run(
            serie, burst, stop,
            [&](const std::string& n, int ep, const std::string& m) {
                if (onProgress)
                    onProgress(n, ep, m);
            },
            [&](const std::string& s) {
                if (onOverall)
                    onOverall(s);
            },
            [&](const TaskReport& r) { esito.reports.push_back(r); },
            [&](const std::string&, const std::string&) {}, nullptr);

        std::map<std::string, int> maxEpisodes;
        for (const auto& r : esito.reports) {
            if (r.success && r.episodeNumber > 0)
                maxEpisodes[r.name] = (std::max)(maxEpisodes[r.name], r.episodeNumber);
        }
        if (!maxEpisodes.empty()) {
            const auto now = std::chrono::system_clock::now();
            const auto tt = std::chrono::system_clock::to_time_t(now);
            const auto tm = *std::gmtime(&tt);
            std::array<char, 24> ts{};
            std::strftime(ts.data(), ts.size(), "%Y-%m-%dT%H:%M:%SZ", &tm);
            repo.applyDownloadedEpisodes(maxEpisodes, ts.data());
        }

        std::size_t ok = 0;
        for (const auto& r : esito.reports)
            ok += r.success ? 1 : 0;
        esito.eseguito = true;
        esito.nota = "giro completato: " + std::to_string(ok) + "/" +
                     std::to_string(esito.reports.size()) + " riusciti";
        Logger::info("CheckRunner: " + esito.nota);
        return esito;
    }

} // namespace Core
