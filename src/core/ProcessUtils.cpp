#include "core/ProcessUtils.hpp"
#include "scrapers/ScraperUtils.hpp"
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdio>
#include <format>
#include <fstream>
#include <memory>
#include <array>
#ifndef _WIN32
#include <csignal>
#include <sys/poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Core {
    namespace ProcessUtils {

        int runCommand(const std::string& cmd, std::atomic<bool>& stopSignal,
                       const std::function<void(const std::string&)>& onLineRead) {
            std::string fullCmd = cmd + " 2>&1";

#ifdef _WIN32
            FILE* rawPipe = ScraperUtils::popenCompat(fullCmd, "r");
            if (!rawPipe)
                return -1;
            std::unique_ptr<FILE, decltype(&ScraperUtils::pcloseCompat)> pipe(
                rawPipe, ScraperUtils::pcloseCompat);
            char buffer[512];
            while (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) {
                if (stopSignal)
                    break;
                if (onLineRead)
                    onLineRead(std::string(buffer));
            }
            pipe.release();
            return ScraperUtils::pcloseCompat(rawPipe);
#else
            std::array<int, 2> pipefd{};
            if (pipe(pipefd.data()) == -1)
                return -1;

            pid_t pid = fork();
            if (pid == -1) {
                close(pipefd[0]);
                close(pipefd[1]);
                return -1;
            }

            if (pid == 0) {
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                dup2(pipefd[1], STDERR_FILENO);
                close(pipefd[1]);
                execl("/bin/sh", "sh", "-c", fullCmd.c_str(), nullptr);
                _exit(127);
            }

            close(pipefd[1]);

            int fd = pipefd[0];
            std::array<char, 4096> buf{};
            std::string lineBuf;
            bool stopped = false;

            while (true) {
                if (!stopped && stopSignal) {
                    stopped = true;
                    kill(pid, SIGTERM);
                    usleep(500000);
                    int ws;
                    if (waitpid(pid, &ws, WNOHANG) == 0)
                        kill(pid, SIGKILL);
                }
                if (stopped)
                    break;

                struct pollfd pfd = {fd, POLLIN, 0};
                int ret = poll(&pfd, 1, 200);

                if (ret > 0) {
                    if (pfd.revents & POLLIN) {
                        ssize_t n = read(fd, buf.data(), buf.size() - 1);
                        if (n > 0) {
                            buf[n] = '\0';
                            lineBuf.append(buf.data(), static_cast<size_t>(n));
                            size_t pos;
                            while ((pos = lineBuf.find('\n')) != std::string::npos) {
                                std::string line = lineBuf.substr(0, pos);
                                lineBuf.erase(0, pos + 1);
                                if (onLineRead)
                                    onLineRead(line);
                            }
                        } else if (n == 0) {
                            break;
                        }
                    }
                    if (pfd.revents & (POLLHUP | POLLERR)) {
                        break;
                    }
                } else if (ret < 0 && errno != EINTR) {
                    break;
                }
            }

            if (!lineBuf.empty() && onLineRead)
                onLineRead(lineBuf);
            close(fd);

            if (stopped) {
                waitpid(pid, nullptr, 0);
                return -1;
            }

            int status;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status))
                return WEXITSTATUS(status);
            return -1;
#endif
        }

        double getRamUsagePercent() {
#ifdef _WIN32
            MEMORYSTATUSEX memInfo;
            memInfo.dwLength = sizeof(MEMORYSTATUSEX);
            if (GlobalMemoryStatusEx(&memInfo)) {
                return static_cast<double>(memInfo.dwMemoryLoad);
            }
            return 100.0;
#else
            std::ifstream meminfo("/proc/meminfo");
            if (!meminfo.is_open())
                return 100.0;
            std::string line;
            long total = 1, available = 0;
            while (std::getline(meminfo, line)) {
                if (line.find("MemTotal:") == 0)
                    std::sscanf(line.c_str(), "MemTotal: %ld", &total);
                if (line.find("MemAvailable:") == 0)
                    std::sscanf(line.c_str(), "MemAvailable: %ld", &available);
            }
            return ((double)(total - available) / total) * 100.0;
#endif
        }

        double parseProgressUs(const std::filesystem::path& progressFile) {
            if (!std::filesystem::exists(progressFile))
                return 0;
            std::ifstream f(progressFile);
            std::string line;
            long long t = 0;
            while (std::getline(f, line))
                if (line.find("out_time_us=") == 0)
                    try {
                        t = std::stoll(line.substr(12));
                    } catch (...) {
                    } // intenzionale: parse numerico, rumore senza valore
            return static_cast<double>(t);
        }

        std::string formatFloat(double value, int precision) {
            // Locale C implicito in std::format: nessun separatore migliaia,
            // stesso arrotondamento (half-to-even) dello storico.
            return std::format("{:.{}f}", value, precision);
        }

    } // namespace ProcessUtils
} // namespace Core
