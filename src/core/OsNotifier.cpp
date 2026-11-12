#include "core/OsNotifier.hpp"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace Core {

#ifdef __linux__
    namespace {
        constexpr int kTimeoutMs = 3000;

        std::string findInPath(const std::string& name) {
            const char* pathEnv = std::getenv("PATH");
            const std::string path = pathEnv != nullptr ? pathEnv : "/usr/bin:/bin";
            std::size_t start = 0;
            while (start <= path.size()) {
                const auto end = path.find(':', start);
                const std::string dir =
                    path.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (!dir.empty()) {
                    std::string full = dir;
                    full += '/';
                    full += name;
                    if (::access(full.c_str(), X_OK) == 0)
                        return full;
                }
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
            return {};
        }

        // Le stringhe passate a gdbus sono GVariant testuali: vanno quotate
        // e i caratteri di controllo rimossi. notify-send riceve argv
        // separati, quindi non serve escaping.
        std::string gvariantQuote(const std::string& value) {
            std::string out = "\"";
            for (const char c : value) {
                if (c == '\\' || c == '"')
                    out += '\\';
                if (c == '\n' || c == '\r' || c == '\t')
                    out += ' ';
                else
                    out += c;
            }
            out += '"';
            return out;
        }

        struct SpawnResult {
            bool started{false};
            int status{-1};
            std::string error;
        };

        SpawnResult spawnWait(const std::string& program, const std::vector<std::string>& args,
                              bool detach) {
            SpawnResult out;
            std::vector<std::string> storage = args;
            storage.insert(storage.begin(), program);
            std::vector<char*> argv;
            argv.reserve(storage.size() + 1);
            for (auto& arg : storage)
                argv.push_back(arg.data());
            argv.push_back(nullptr);

            posix_spawn_file_actions_t actions;
            posix_spawn_file_actions_init(&actions);
            posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
            posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

            pid_t pid = -1;
            const int rc =
                posix_spawnp(&pid, program.c_str(), &actions, nullptr, argv.data(), environ);
            posix_spawn_file_actions_destroy(&actions);
            if (rc != 0) {
                out.error = std::strerror(rc);
                return out;
            }
            out.started = true;

            if (detach) {
                // notify-send --wait deve restare vivo mentre la notifica è
                // visibile: il thread lo attende senza bloccare il chiamante.
                std::thread([pid] {
                    int status = 0;
                    (void)::waitpid(pid, &status, 0);
                }).detach();
                return out;
            }

            int status = 0;
            int waited = 0;
            while (waited < kTimeoutMs) {
                const pid_t r = ::waitpid(pid, &status, WNOHANG);
                if (r == pid) {
                    out.status = status;
                    return out;
                }
                if (r < 0 && errno != EINTR) {
                    out.error = std::strerror(errno);
                    return out;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                waited += 25;
            }
            ::kill(pid, SIGTERM);
            (void)::waitpid(pid, &status, 0);
            out.error = "timeout";
            return out;
        }

        bool exitedOk(const SpawnResult& r) {
            return r.started && WIFEXITED(r.status) && WEXITSTATUS(r.status) == 0;
        }
    } // namespace
#endif

    bool OsNotifier::supported() noexcept {
#ifdef __linux__
        return true;
#else
        return false;
#endif
    }

    bool OsNotifier::available() {
#ifdef __linux__
        return !findInPath("notify-send").empty() || !findInPath("gdbus").empty();
#else
        return false;
#endif
    }

    OsNotifier::Result OsNotifier::send(const std::string& title, const std::string& body) {
#ifdef __linux__
        const std::string notifySend = findInPath("notify-send");
        if (!notifySend.empty()) {
            const auto r =
                spawnWait(notifySend,
                          {"--app-name=AniDownloader", "--icon=com.anidownloader.desktop",
                           "--urgency=normal", "--expire-time=6000", "--wait", "--", title, body},
                          true);
            if (r.started)
                return {true, "notify-send", ""};
            return {false, "notify-send", r.error};
        }

        const std::string gdbus = findInPath("gdbus");
        if (!gdbus.empty()) {
            const auto r =
                spawnWait(gdbus,
                          {"call", "--session", "--dest", "org.freedesktop.Notifications",
                           "--object-path", "/org/freedesktop/Notifications", "--method",
                           "org.freedesktop.Notifications.Notify", "--timeout", "3",
                           "AniDownloader", "0", "com.anidownloader.desktop", gvariantQuote(title),
                           gvariantQuote(body), "@as []", "@a{sv} {}", "6000"},
                          false);
            if (exitedOk(r))
                return {true, "gdbus", ""};
            return {false, "gdbus", r.error.empty() ? "exit " + std::to_string(r.status) : r.error};
        }

        return {false, "", "né notify-send né gdbus nel PATH"};
#else
        (void)title;
        (void)body;
        return {false, "", "notifiche di sistema dal demone non supportate su questo OS"};
#endif
    }

} // namespace Core
