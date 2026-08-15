#include "core/InstanceLock.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace Core {

    struct InstanceLock::Impl {
#ifdef _WIN32
        HANDLE handle = INVALID_HANDLE_VALUE;
#else
        int fd = -1;
#endif
    };

    InstanceLock::InstanceLock(const std::string& lockPath) {
        auto* impl = new Impl();
#ifdef _WIN32
        // Share mode 0 = nessun accesso condiviso: una seconda apertura da un
        // altro processo fallisce con ERROR_SHARING_VIOLATION. Il parametro
        // lpSecurityAttributes è nullptr → l'handle non è ereditabile.
        impl->handle = CreateFileA(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE,
                                   0 /* no sharing */, nullptr, OPEN_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
        if (impl->handle == INVALID_HANDLE_VALUE) {
            delete impl;
            m_impl = nullptr;
            return;
        }
#else
        impl->fd = ::open(lockPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (impl->fd < 0) {
            delete impl;
            m_impl = nullptr;
            return;
        }
        // LOCK_NB: se un altro processo (o un'altra open description) tiene
        // già il lock → fallisce subito. LOCK_UN è implicito alla chiusura.
        if (::flock(impl->fd, LOCK_EX | LOCK_NB) != 0) {
            ::close(impl->fd);
            delete impl;
            m_impl = nullptr;
            return;
        }
#endif
        m_impl = impl;
    }

    InstanceLock::~InstanceLock() {
        if (!m_impl)
            return;
#ifdef _WIN32
        CloseHandle(m_impl->handle);
#else
        ::flock(m_impl->fd, LOCK_UN);
        ::close(m_impl->fd);
#endif
        delete m_impl;
    }

    bool InstanceLock::acquired() const noexcept { return m_impl != nullptr; }

} // namespace Core
