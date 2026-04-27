/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * Asyncronous BSD sockets file download benchmark with epoll()
 * Minimal raw-byte benchmark for performance and energy measurements.
 */

#include <benchmark/benchmark.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

// Default server TCP port.
constexpr int DEFAULT_PORT = 8080;

// Runtime-configurable server port.
static int g_port = DEFAULT_PORT;

// Fixed receive buffer size.
constexpr std::size_t BUFFER_SIZE = 8192;

// Set a socket to non-blocking mode.
static bool set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

// Wait until a non-blocking connect operation completes.
static bool wait_for_connect(int fd) {
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLOUT;

    while (true) {
        const int ready = poll(&pfd, 1, -1);

        if (ready > 0) {
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                return false;
            }

            if (pfd.revents & POLLOUT) {
                int so_error = 0;
                socklen_t len = sizeof(so_error);

                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) == -1) {
                    return false;
                }

                return so_error == 0;
            }
        } else if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
    }
}

// Create and connect a non-blocking TCP socket.
static int connect_tcp(const char* ip, int port) {
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        return -1;
    }

    if (!set_nonblocking(sock)) {
        close(sock);
        return -1;
    }

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(static_cast<std::uint16_t>(port));

    if (inet_pton(AF_INET, ip, &server.sin_addr) <= 0) {
        close(sock);
        return -1;
    }

    if (connect(sock, reinterpret_cast<sockaddr*>(&server), sizeof(server)) == -1) {
        if (errno != EINPROGRESS) {
            close(sock);
            return -1;
        }

        if (!wait_for_connect(sock)) {
            close(sock);
            return -1;
        }
    }

    return sock;
}

// Download raw bytes until the peer closes the connection.
static bool download_file(const char* ip, int port, std::array<char, BUFFER_SIZE>& buffer) {
    const int sock = connect_tcp(ip, port);
    if (sock == -1) {
        return false;
    }

    while (true) {
        const ssize_t n = recv(sock, buffer.data(), buffer.size(), 0);

        if (n > 0) {
            continue;
        }

        if (n == 0) {
            close(sock);
            benchmark::DoNotOptimize(buffer.data());
            benchmark::ClobberMemory();
            return true;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            pollfd pfd{};
            pfd.fd = sock;
            pfd.events = POLLIN;

            while (true) {
                const int ready = poll(&pfd, 1, -1);

                if (ready > 0) {
                    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                        close(sock);
                        return false;
                    }

                    if (pfd.revents & POLLIN) {
                        break;
                    }
                } else if (ready == -1) {
                    if (errno == EINTR) {
                        continue;
                    }

                    close(sock);
                    return false;
                }
            }

            continue;
        }

        close(sock);
        return false;
    }
}

// Benchmark one full file download.
static void BM_TCP_FileDownload(benchmark::State& state) {
    constexpr const char* ip = "127.0.0.1";
    const int port = g_port;

    std::array<char, BUFFER_SIZE> buffer{};

    for (auto _ : state) {
        if (!download_file(ip, port, buffer)) {
            state.SkipWithError("Download failed.");
            break;
        }
    }
}

BENCHMARK(BM_TCP_FileDownload)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(1)
    ->UseRealTime();

int main(int argc, char** argv) {
    const std::string prefix = "--server_port=";

    int filtered_argc = 1;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg.rfind(prefix, 0) == 0) {
            g_port = std::stoi(arg.substr(prefix.size()));
        } else {
            argv[filtered_argc++] = argv[i];
        }
    }

    argv[filtered_argc] = nullptr;

    benchmark::Initialize(&filtered_argc, argv);
    if (benchmark::ReportUnrecognizedArguments(filtered_argc, argv)) {
        return 1;
    }

    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}