#pragma once

#include <string>

namespace Core {
    // Lock esclusivo di processo non bloccante su un lockfile, stile
    // "transaction guard" (feature 11): copre snapshot/planning → download →
    // commit dello stato. Il kernel rilascia il lock alla morte del processo
    // (niente stale detection, niente PID reuse).
    //
    // POSIX: flock(LOCK_EX|LOCK_NB) su fd aperto con O_CLOEXEC (il lock non
    // viene ereditato dai figli). Windows: CreateFileA con share mode 0
    // (apertura esclusiva; handle non ereditabile di default).
    class InstanceLock {
      public:
        explicit InstanceLock(const std::string& lockPath);
        ~InstanceLock();
        InstanceLock(const InstanceLock&) = delete;
        InstanceLock& operator=(const InstanceLock&) = delete;

        [[nodiscard]] bool acquired() const noexcept;

      private:
        struct Impl;
        Impl* m_impl;
    };
} // namespace Core
