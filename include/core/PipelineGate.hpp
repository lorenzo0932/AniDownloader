#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>

// Coordinatore di esecuzione del demone (pipeline condivisa, fase 1).
// Vincolo: nel processo gira UNA sola esecuzione alla volta (check schedulato
// o run manuale). Le due origini non lanciano due ExecutionEngine concorrenti:
// - l'interattivo ha precedenza per il turno successivo;
// - il background aspetta se c'è un interattivo in attesa (niente starvation
//   dell'interattivo dietro una coda di check);
// - nessuna preemption: chi sta girando finisce il suo turno.
// Il gate è posseduto dal WebServer (lifetime esplicito), mai un singleton.
namespace Core {

    class PipelineGate {
      public:
        // Rilascia automaticamente alla distruzione (RAII, anche su eccezione).
        class Guard {
          public:
            Guard() = default;
            explicit Guard(PipelineGate* gate) : m_gate(gate) {}
            Guard(const Guard&) = delete;
            Guard& operator=(const Guard&) = delete;
            Guard(Guard&& other) noexcept : m_gate(other.m_gate) { other.m_gate = nullptr; }
            Guard& operator=(Guard&& other) noexcept {
                if (this != &other) {
                    release();
                    m_gate = other.m_gate;
                    other.m_gate = nullptr;
                }
                return *this;
            }
            ~Guard() { release(); }

            [[nodiscard]] bool owns() const noexcept { return m_gate != nullptr; }

          private:
            void release() {
                if (m_gate != nullptr) {
                    m_gate->release();
                    m_gate = nullptr;
                }
            }
            PipelineGate* m_gate{nullptr};
        };

        PipelineGate() = default;
        PipelineGate(const PipelineGate&) = delete;
        PipelineGate& operator=(const PipelineGate&) = delete;

        // Blocca finché il turno è disponibile. interactive=true ha precedenza
        // sul background ancora da iniziare. abortFlag (opzionale) interrompe
        // l'attesa quando vale true (polarità ABORT, non "running"): in quel
        // caso il Guard ritornato non possiede il turno. Non passare flag che
        // sono true durante il funzionamento normale.
        [[nodiscard]] Guard acquire(bool interactive, std::atomic<bool>* abortFlag = nullptr);

        [[nodiscard]] bool inEsecuzione() const;
        [[nodiscard]] bool interattivoInAttesa() const;

      private:
        void release();

        mutable std::mutex m_mutex;
        std::condition_variable m_cv;
        bool m_running{false};
        bool m_interattivoInAttesa{false};
    };

} // namespace Core
