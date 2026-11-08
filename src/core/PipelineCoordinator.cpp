#include "core/PipelineCoordinator.hpp"

#include "core/Logger.hpp"
#include "core/MediaProcessor.hpp"
#include "core/Series.hpp"

#include <nlohmann/json.hpp>

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

    void PipelineCoordinator::start(const PipelineRunContext& ctx) {
        if (m_attivo.exchange(true))
            return;
        m_ctx = ctx;
        m_usaCtx = true;
        for (int i = 0; i < m_downloadThreads; ++i)
            m_threads.emplace_back([this] { cicloDownloadCtx(); });
        for (int i = 0; i < m_conversionThreads; ++i)
            m_threads.emplace_back([this] { cicloConversioneCtx(); });
    }

    namespace {
        void retryOrFailCtx(PipelineJobStore& store, const PipelineJob& job,
                            const std::string& errore) {
            if (job.tentativi >= PipelineJobStore::kMaxTentativi) {
                store.fail(job.id, errore);
                return;
            }
            const auto i = static_cast<std::size_t>((std::min)(job.tentativi, 2));
            store.retry(job.id, PipelineCoordinator::kBackoffSecondi[i], errore);
        }
    } // namespace

    void PipelineCoordinator::cicloDownloadCtx() {
        PipelineJobStore store(m_dbPath);
        while (m_attivo.load()) {
            store.promuoviRetry();
            auto job = store.claim("queued_download", "downloading");
            if (!job.has_value()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            DownloadTask task;
            task.shouldProcess = true;
            task.episodeNumber = job->episodio;
            try {
                const auto payload = nlohmann::json::parse(job->payload);
                task.videoUrl = payload.value("videoUrl", "");
                task.fileName = payload.value("fileName", "");
            } catch (const std::exception&) {
                retryOrFailCtx(store, *job, "payload job illeggibile");
                continue;
            }
            if (task.fileName.empty() || task.videoUrl.empty()) {
                // Job senza dati sufficienti: mai passarlo a MediaProcessor
                // (un fileName vuoto punterebbe alla cartella serie).
                retryOrFailCtx(store, *job, "payload incompleto");
                continue;
            }
            Series serie;
            serie.name = job->serie;
            serie.path = job->seriePath;
            auto progress = [this, ep = job->episodio](const std::string& n, const std::string& m) {
                if (m_ctx.onProgress)
                    m_ctx.onProgress(n, ep, m);
            };
            MediaProcessor mp(progress, *m_ctx.stop,
                              [this, n = job->serie, ep = job->episodio](const TaskProgress& p) {
                                  if (m_ctx.onActivity)
                                      m_ctx.onActivity(n, ep, p);
                              });
            auto strategyDl = m_ctx.strategy;
            strategyDl.convertToH265 = false;
            ProcessResult res;
            try {
                res = mp.processTask(task, serie, strategyDl);
            } catch (const std::exception& e) {
                // Un job rotto non deve mai abbattere il demone.
                retryOrFailCtx(store, *job, std::string("eccezione: ") + e.what());
                continue;
            }
            if (!res.success) {
                retryOrFailCtx(store, *job, res.errorMessage);
                continue;
            }
            store.setFilePath(job->id, res.filePath);
            if (m_ctx.strategy.convertToH265) {
                store.transition(job->id, "downloading", "downloaded_waiting_conversion");
            } else {
                store.transition(job->id, "downloading", "completed");
                if (m_ctx.onCompleted)
                    m_ctx.onCompleted(job->serie, job->episodio);
                if (m_ctx.onFinished)
                    m_ctx.onFinished(job->serie, job->episodio, res.downloadTime, 0.0);
            }
        }
    }

    void PipelineCoordinator::cicloConversioneCtx() {
        PipelineJobStore store(m_dbPath);
        while (m_attivo.load()) {
            auto job = store.claim("downloaded_waiting_conversion", "converting");
            if (!job.has_value()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            Series serie;
            serie.name = job->serie;
            serie.path = job->seriePath;
            auto progress = [this, ep = job->episodio](const std::string& n, const std::string& m) {
                if (m_ctx.onProgress)
                    m_ctx.onProgress(n, ep, m);
            };
            MediaProcessor mp(progress, *m_ctx.stop,
                              [this, n = job->serie, ep = job->episodio](const TaskProgress& p) {
                                  if (m_ctx.onActivity)
                                      m_ctx.onActivity(n, ep, p);
                              });
            double convTime = 0.0;
            bool ok = false;
            try {
                ok = !job->filePath.empty() &&
                     mp.convertFile(job->filePath, job->serie, m_ctx.strategy, convTime);
            } catch (const std::exception& e) {
                retryOrFailCtx(store, *job, std::string("eccezione: ") + e.what());
                continue;
            }
            if (!ok) {
                retryOrFailCtx(store, *job, "conversione fallita");
                continue;
            }
            store.transition(job->id, "converting", "completed");
            if (m_ctx.onCompleted)
                m_ctx.onCompleted(job->serie, job->episodio);
            if (m_ctx.onFinished)
                m_ctx.onFinished(job->serie, job->episodio, 0.0, convTime);
        }
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
