#pragma once

#include <functional>
#include <string>

namespace Core {
    // Stato osservativo dell'episodio, non stato della coda persistente.
    enum class TaskPhase {
        Queued,
        Downloading,
        VerifyingDownload,
        WaitingConversion,
        Converting,
        Finalizing,
        Error
    };

    inline const char* phaseName(TaskPhase phase) {
        switch (phase) {
        case TaskPhase::Queued:
            return "queued";
        case TaskPhase::Downloading:
            return "downloading";
        case TaskPhase::VerifyingDownload:
            return "verifying_download";
        case TaskPhase::WaitingConversion:
            return "waiting_conversion";
        case TaskPhase::Converting:
            return "converting";
        case TaskPhase::Finalizing:
            return "finalizing";
        case TaskPhase::Error:
            return "error";
        }
        return "queued";
    }

    struct TaskProgress {
        TaskPhase phase;
        int percent = -1; // -1: operazione senza percentuale misurabile
        std::string detail;
    };
    using TaskProgressCallback = std::function<void(const TaskProgress&)>;
} // namespace Core
