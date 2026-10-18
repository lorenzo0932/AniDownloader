#pragma once

#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace Web {
    // Proiezione osservativa del run manuale. Il chiamante serializza record e
    // snapshot con lo stesso mutex. Non pilota worker e non scrive nel DB.
    class DownloadState {
      public:
        using Json = nlohmann::json;

        static std::string key(const std::string& series, int episode) {
            return Json::array({series, episode}).dump();
        }

        bool record(Json& event) {
            const auto type = event.value("type", "");
            if (type != "started" && type != "progress" && type != "overall" && type != "phase" &&
                type != "finished" && type != "skipped" && type != "done")
                return false;
            if (type == "started") {
                ++m_run;
                m_tasks = Json::object();
                m_events = Json::array();
                m_running = true;
                m_phase = "analysis";
                m_status = "Analisi in corso";
                m_stopped = false;
            }
            event["sequence"] = ++m_sequence;
            event["runId"] = m_run;
            event["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count();

            bool significant = type == "started" || type == "done";
            if (type == "progress" || type == "finished" || type == "skipped") {
                auto name = event.value("series", "");
                int ep = event.value("episode", 0);
                auto id = key(name, ep);
                const auto old = m_tasks.value(id, Json::object());
                std::string phase;
                if (type == "finished")
                    phase = event.value("success", false)       ? "completed"
                            : event.value("interrupted", false) ? "interrupted"
                                                                : "error";
                else if (type == "skipped")
                    phase = "skipped";
                else
                    phase = event.value("phase", "analysing");
                // Il dettaglio ep=0 non deve sopravvivere all'analisi della serie.
                if (ep > 0)
                    m_tasks.erase(key(name, 0));
                auto detail = event.value("detail", event.value("message", ""));
                if (type == "finished")
                    detail =
                        event.value("success", false) ? "Pronto" : event.value("error", "Errore");
                if (type == "skipped")
                    detail = event.value("reason", "Già aggiornata");
                int percent = phase == "completed" ? 100 : event.value("percent", -1);
                m_tasks[id] = {{"key", id},
                               {"seriesName", name},
                               {"episode", ep},
                               {"phase", phase},
                               {"percent", percent},
                               {"detail", detail},
                               {"dlTime", event.value("dlTime", 0.0)},
                               {"convTime", event.value("convTime", 0.0)}};
                significant = old.value("phase", "") != phase;
                event["task"] = m_tasks[id];
            } else if (type == "overall") {
                m_status = event.value("status", "");
            } else if (type == "phase") {
                m_phase = event.value("phase", "processing");
            } else if (type == "done") {
                m_running = false;
                m_stopped = event.value("stopped", false);
                m_phase = "done";
                m_status = m_stopped ? "Elaborazione interrotta" : "Elaborazione terminata";
                for (auto& task : m_tasks) {
                    const auto phase = task.value("phase", "");
                    if (phase != "completed" && phase != "error" && phase != "skipped" &&
                        phase != "interrupted") {
                        task["phase"] = m_stopped ? "interrupted" : "error";
                        task["percent"] = -1;
                        task["detail"] =
                            m_stopped ? "Elaborazione interrotta" : "Esito non disponibile";
                    }
                }
            }
            if (significant) {
                Json log = {{"id", m_sequence},
                            {"timestamp", event["timestamp"]},
                            {"type", type},
                            {"series", event.value("series", "")},
                            {"episode", event.value("episode", 0)}};
                if (event.contains("task"))
                    log["task"] = event["task"];
                if (type == "done")
                    log["stopped"] = m_stopped;
                m_events.push_back(log);
                if (m_events.size() > 120)
                    m_events.erase(m_events.begin());
                event["log"] = log;
            }
            return true;
        }

        Json snapshot() const {
            Json tasks = Json::array();
            for (const auto& task : m_tasks)
                tasks.push_back(task);
            return {{"running", m_running}, {"runId", m_run},     {"sequence", m_sequence},
                    {"phase", m_phase},     {"status", m_status}, {"stopped", m_stopped},
                    {"tasks", tasks},       {"events", m_events}};
        }

      private:
        std::uint64_t m_sequence = 0;
        std::uint64_t m_run = 0;
        bool m_running = false;
        bool m_stopped = false;
        std::string m_phase = "idle";
        std::string m_status = "Pronto.";
        Json m_tasks = Json::object();
        Json m_events = Json::array();
    };
} // namespace Web
