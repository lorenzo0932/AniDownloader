#include "core/Scheduler.hpp"

#include "config/AppConfigManager.hpp"
#include "core/CheckRunner.hpp"
#include "core/Database.hpp"
#include "core/ExecutionEngine.hpp"
#include "core/Logger.hpp"
#include "core/QueueStore.hpp"
#include "core/SeriesRepository.hpp"

#include <algorithm>
#include <chrono>

namespace Core {

    namespace {
        constexpr const char* kChiaveUltimoGiro = "scheduler_ultimo_giro";

        std::int64_t oraEpoch() {
            return std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }

        std::int64_t leggiUltimoGiro(const std::string& dbPath) {
            Database db(dbPath);
            if (!db.isOpen())
                return 0;
            auto sel = db.prepare("SELECT value FROM kv WHERE key = ?;");
            if (!sel.valid() || !sel.bindText(1, kChiaveUltimoGiro) || !sel.step() ||
                sel.hasError())
                return 0;
            try {
                return std::stoll(sel.columnText(0));
            } catch (const std::exception&) {
                return 0;
            }
        }

        void scriviUltimoGiro(const std::string& dbPath, std::int64_t ora) {
            Database db(dbPath);
            if (!db.isOpen())
                return;
            auto upd = db.prepare("INSERT OR REPLACE INTO kv(key, value) VALUES (?, ?);");
            if (!upd.valid() || !upd.bindText(1, kChiaveUltimoGiro) ||
                !upd.bindText(2, std::to_string(ora)) || (upd.step(), upd.hasError()))
                Logger::error("Scheduler: salvataggio ultimo giro fallito: " + upd.lastError());
        }
    } // namespace

    Scheduler::Scheduler(Config::AppConfigManager& config, const std::string& jsonPath)
        : m_config(config), m_jsonPath(jsonPath),
          m_dbPath(SeriesRepository::dbPathFor(jsonPath).string()) {}

    Scheduler::~Scheduler() { stop(); }

    void Scheduler::start() {
        if (m_attivo.exchange(true))
            return;
        if (!m_dbPath.empty()) {
            QueueStore coda(m_dbPath);
            if (!coda.resetOrfani())
                Logger::error("Scheduler: reset orfani fallito: " + coda.lastError());
        }
        m_thread = std::thread([this] { ciclo(); });
    }

    void Scheduler::stop() {
        if (!m_attivo.exchange(false))
            return;
        // Il giro in corso (se c'è) finisce da solo: join senza abortire.
        if (m_thread.joinable())
            m_thread.join();
    }

    bool Scheduler::running() const noexcept { return m_attivo.load(); }

    bool Scheduler::checkMaturo(std::int64_t ora) {
        if (m_dbPath.empty())
            return false;
        m_config.reloadConfig();
        const auto sched = m_config.get<nlohmann::json>(
            "scheduling", {{"abilitato", true}, {"intervalloMinuti", kDefaultIntervalloMinuti}});
        if (!sched.value("abilitato", true))
            return false;
        const int minuti = std::clamp(sched.value("intervalloMinuti", kDefaultIntervalloMinuti),
                                      kMinIntervalloMinuti, kMaxIntervalloMinuti);
        const std::int64_t ultimo = m_dbPath.empty() ? 0 : leggiUltimoGiro(m_dbPath);
        if (ora - ultimo < static_cast<std::int64_t>(minuti) * 60)
            return false;
        // Mai due check in coda: se il precedente deve ancora girare, il
        // prossimo giro slitta (niente accumulo dopo sleep lunghi).
        if (!m_dbPath.empty()) {
            QueueStore coda(m_dbPath);
            if (coda.pendingCount("check") > 0)
                return false;
            if (coda.enqueue("check", "", 0, "{}", 0) == 0) {
                Logger::error("Scheduler: enqueue check fallito: " + coda.lastError());
                return false;
            }
        }
        if (!m_dbPath.empty())
            scriviUltimoGiro(m_dbPath, ora);
        return true;
    }

    void Scheduler::eseguiTask() {
        if (m_dbPath.empty())
            return;
        QueueStore coda(m_dbPath);
        auto task = coda.claim();
        if (!task.has_value())
            return;
        std::atomic<bool> maiAbortito{false}; // 2-bis: il giro non si interrompe
        CheckOutcome esito = CheckRunner::runOnce(m_config, m_jsonPath, false, maiAbortito);
        if (!esito.eseguito) {
            if (esito.salto == CheckOutcome::Salto::Occupato) {
                // CLI manuale in corso: si rimanda senza consumare il task.
                if (!coda.failRetry(task->id, kRetryOccupatoSecondi))
                    Logger::error("Scheduler: retry occupato fallito: " + coda.lastError());
                return;
            }
            if (!coda.failDead(task->id))
                Logger::error("Scheduler: failDead fallito: " + coda.lastError());
            Logger::error("Scheduler: giro fallito: " + esito.nota);
            return;
        }
        // Giro eseguito anche a reports vuoti (serie già aggiornate): il
        // task ha fatto il suo dovere.
        if (!coda.complete(task->id))
            Logger::error("Scheduler: complete fallito: " + coda.lastError());
        Logger::info("Scheduler: " + esito.nota);
    }

    void Scheduler::ciclo() {
        Logger::info("Scheduler: avviato (tick " + std::to_string(kTickSecondi) + "s)");
        while (m_attivo.load()) {
            // Catch-up: al primo tick dopo avvio/sleep, se l'intervallo è
            // passato parte subito un giro (mai arretrati accumulati).
            if (checkMaturo(oraEpoch()))
                Logger::info("Scheduler: check schedulato accodato");
            eseguiTask();
            for (int i = 0; i < kTickSecondi && m_attivo.load(); ++i)
                std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        Logger::info("Scheduler: fermato");
    }

} // namespace Core
