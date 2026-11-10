#include "core/PipelineGate.hpp"

#include <chrono>

namespace Core {

    PipelineGate::Guard PipelineGate::acquire(bool interactive, std::atomic<bool>* abortFlag) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (interactive)
            m_interattivoInAttesa = true;
        auto disponibile = [this, interactive] {
            if (m_running)
                return false;
            // Il background lascia passare prima l'interattivo in attesa.
            return interactive || !m_interattivoInAttesa;
        };
        while (!disponibile()) {
            if (abortFlag != nullptr && abortFlag->load())
                break;
            m_cv.wait_for(lock, std::chrono::milliseconds(200));
        }
        if (abortFlag != nullptr && abortFlag->load()) {
            if (interactive)
                m_interattivoInAttesa = false;
            return Guard{};
        }
        m_running = true;
        if (interactive)
            m_interattivoInAttesa = false;
        return Guard{this};
    }

    void PipelineGate::release() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_running = false;
        }
        m_cv.notify_all();
    }

    bool PipelineGate::inEsecuzione() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_running;
    }

    bool PipelineGate::interattivoInAttesa() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_interattivoInAttesa;
    }

} // namespace Core
