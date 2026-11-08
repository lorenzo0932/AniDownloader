#include "core/PipelineCoordinator.hpp"

#include "core/Logger.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace Core {

    PipelineCoordinator::PipelineCoordinator(std::filesystem::path dbPath, int downloadThreads,
                                             int conversionThreads)
        : m_dbPath(std::move(dbPath)), m_downloadThreads((std::max)(1, downloadThreads)),
          m_conversionThreads((std::max)(1, conversionThreads)) {}

    PipelineCoordinator::~PipelineCoordinator() { stop(); }

    void PipelineCoordinator::setDownloadHandler(Handler handler) {
        m_downloadHandler = std::move(handler);
    }

    void PipelineCoordinator::setConversionHandler(Handler handler) {
        m_conversionHandler = std::move(handler);
    }

    bool PipelineCoordinator::attivo() const noexcept { return m_attivo.load(); }

    PipelineCoordinator::Stato PipelineCoordinator::stato() const {
        Stato s;
        s.attivo = m_attivo.load();
        s.downloadWorker = m_downloadThreads;
        s.conversionWorker = m_conversionThreads;
        return s;
    }

    std::map<std::string, int> PipelineCoordinator::counts() const {
        PipelineJobStore store(m_dbPath);
        return store.counts();
    }

    void PipelineCoordinator::start() {
        if (m_attivo.exchange(true))
            return;
        for (int i = 0; i < m_downloadThreads; ++i)
            m_threads.emplace_back([this] { cicloDownload(); });
        for (int i = 0; i < m_conversionThreads; ++i)
            m_threads.emplace_back([this] { cicloConversione(); });
    }

    void PipelineCoordinator::stop() {
        if (!m_attivo.exchange(false))
            return;
        for (auto& t : m_threads) {
            if (t.joinable())
                t.join();
        }
        m_threads.clear();
    }

    void PipelineCoordinator::cicloDownload() {
        PipelineJobStore store(m_dbPath);
        while (m_attivo.load()) {
            // I retry scaduti tornano in coda: un solo posto lo fa.
            store.promuoviRetry();
            auto job = store.claim("queued_download", "downloading");
            if (!job.has_value()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(120));
                continue;
            }
            esegui(store, *job, m_downloadHandler, "downloading");
        }
    }

    void PipelineCoordinator::cicloConversione() {
        PipelineJobStore store(m_dbPath);
        while (m_attivo.load()) {
            auto job = store.claim("downloaded_waiting_conversion", "converting");
            if (!job.has_value()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(120));
                continue;
            }
            esegui(store, *job, m_conversionHandler, "converting");
        }
    }

    void PipelineCoordinator::esegui(PipelineJobStore& store, const PipelineJob& job,
                                     const Handler& handler, const std::string& faseCorrente) {
        bool ok = false;
        if (handler) {
            try {
                ok = handler(job);
            } catch (const std::exception& e) {
                Logger::error("Pipeline: handler '" + job.jobKey + "' ha lanciato: " + e.what());
                ok = false;
            }
        }
        if (ok) {
            const std::string prossima =
                faseCorrente == "downloading" ? "downloaded_waiting_conversion" : "completed";
            if (!store.transition(job.id, faseCorrente, prossima))
                Logger::error("Pipeline: transizione fallita per job " + std::to_string(job.id));
            return;
        }
        const int tentativi = job.tentativi;
        if (tentativi >= PipelineJobStore::kMaxTentativi) {
            store.fail(job.id, "tentativi esauriti");
            return;
        }
        const auto indice = static_cast<std::size_t>(tentativi);
        store.retry(job.id, kBackoffSecondi[indice]);
    }

} // namespace Core
