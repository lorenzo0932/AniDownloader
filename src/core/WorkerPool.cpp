#include "core/WorkerPool.hpp"

#include "core/Database.hpp"
#include "core/Logger.hpp"
#include "core/QueueStore.hpp"

#include <algorithm>
#include <chrono>

namespace Core {

    WorkerPool::WorkerPool(const std::string& dbPath, int numThread)
        : m_dbPath(dbPath), m_numThread((std::max)(1, numThread)) {}

    WorkerPool::~WorkerPool() { stop(); }

    void WorkerPool::onKind(const std::string& kind, TaskHandler handler) {
        m_handlers.emplace_back(kind, std::move(handler));
    }

    void WorkerPool::onEvento(TaskEventoCb cb) { m_onEvento = std::move(cb); }

    void WorkerPool::start() {
        if (m_attivo.exchange(true))
            return;
        if (!m_dbPath.empty()) {
            QueueStore coda(m_dbPath);
            if (!coda.resetOrfani())
                Logger::error("WorkerPool: reset orfani fallito: " + coda.lastError());
        }
        for (int i = 0; i < m_numThread; ++i)
            m_threads.emplace_back([this] { ciclo(); });
    }

    void WorkerPool::stop() {
        if (!m_attivo.exchange(false))
            return;
        // Cooperativo: i task in corso finiscono, poi i thread escono.
        for (auto& t : m_threads) {
            if (t.joinable())
                t.join();
        }
        m_threads.clear();
    }

    void WorkerPool::pausa(bool inPausa) { m_pausa.store(inPausa); }

    bool WorkerPool::running() const noexcept { return m_attivo.load(); }

    bool WorkerPool::inPausa() const noexcept { return m_pausa.load(); }

    void WorkerPool::ciclo() {
        QueueStore coda(m_dbPath);
        while (m_attivo.load()) {
            if (!m_pausa.load() && !m_dbPath.empty()) {
                auto task = coda.claim();
                if (task.has_value()) {
                    auto it = std::find_if(m_handlers.begin(), m_handlers.end(),
                                           [&](const auto& h) { return h.first == task->kind; });
                    if (it == m_handlers.end()) {
                        Logger::error("WorkerPool: nessun handler per kind '" + task->kind + "'");
                        coda.failDead(task->id);
                    } else {
                        if (m_onEvento)
                            m_onEvento("avviato", *task);
                        const EsitoTask esito = it->second(*task);
                        if (esito == EsitoTask::Completato) {
                            if (!coda.complete(task->id))
                                Logger::error("WorkerPool: complete fallito: " + coda.lastError());
                            else if (m_onEvento)
                                m_onEvento("completato", *task);
                        } else if (esito == EsitoTask::Riprova) {
                            // Backoff progressivo per tentativi, poi resa.
                            const std::size_t n = static_cast<std::size_t>(task->tentativi);
                            if (n >= sizeof(kBackoffSecondi) / sizeof(kBackoffSecondi[0])) {
                                if (!coda.failDead(task->id))
                                    Logger::error("WorkerPool: failDead fallito: " +
                                                  coda.lastError());
                                else if (m_onEvento)
                                    m_onEvento("fallito", *task);
                            } else {
                                if (!coda.failRetry(task->id, kBackoffSecondi[n]))
                                    Logger::error("WorkerPool: failRetry fallito: " +
                                                  coda.lastError());
                                else if (m_onEvento)
                                    m_onEvento("riprova", *task);
                            }
                        } else {
                            if (!coda.failDead(task->id))
                                Logger::error("WorkerPool: failDead fallito: " + coda.lastError());
                            else if (m_onEvento)
                                m_onEvento("fallito", *task);
                        }
                    }
                    continue; // subito al prossimo claim, niente attesa
                }
            }
            for (int i = 0; i < kPollSecondi && m_attivo.load(); ++i)
                std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

} // namespace Core
