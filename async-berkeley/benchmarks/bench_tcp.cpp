/*
 * Copyright (c) 2026 Jose Antonio Garcia Montanez
 *
 * asyncberkeley raw-byte file download benchmark
 * Minimal benchmark version for performance and energy measurements.
 */

#include <benchmark/benchmark.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <unistd.h>

#include <io/io.hpp>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

using SocketHandle = io::socket::socket_handle;

constexpr int DEFAULT_PORT = 8080;
constexpr std::size_t BUFFER_SIZE = 8192;

static int g_port = DEFAULT_PORT;

static bool set_nonblocking(SocketHandle& fd) {
    const int flags = io::fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }

    return io::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

static int native_fd(const SocketHandle& fd) {
    return static_cast<int>(fd);
}

static bool wait_for_connect(SocketHandle& fd) {
    pollfd pfd{};
    pfd.fd = native_fd(fd);
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

                if (::getsockopt(native_fd(fd), SOL_SOCKET, SO_ERROR, &so_error, &len) == -1) {
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

static SocketHandle connect_to_server(const char* ip, int port) {
    SocketHandle sock(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (native_fd(sock) < 0) {
        return {};
    }

    if (!set_nonblocking(sock)) {
        return {};
    }

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(static_cast<std::uint16_t>(port));

    if (inet_pton(AF_INET, ip, &server.sin_addr) <= 0) {
        return {};
    }

    const auto server_bytes = std::as_bytes(std::span{&server, 1});
    if (io::connect(sock, server_bytes) == -1) {
        if (errno != EINPROGRESS) {
            return {};
        }

        if (!wait_for_connect(sock)) {
            return {};
        }
    }

    return sock;
}

static bool wait_for_readable(SocketHandle& fd) {
    pollfd pfd{};
    pfd.fd = native_fd(fd);
    pfd.events = POLLIN;

    while (true) {
        const int ready = poll(&pfd, 1, -1);

        if (ready > 0) {
            if (pfd.revents & (POLLERR | POLLNVAL)) {
                return false;
            }

            if (pfd.revents & (POLLIN | POLLHUP)) {
                return true;
            }
        } else if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
    }
}

static bool receive_file(SocketHandle& fd,
                         std::array<char, BUFFER_SIZE>& buffer,
                         std::uint64_t& total_bytes) {
    total_bytes = 0;

    while (true) {
        io::socket::socket_message<> msg;
        msg.buffers.emplace_back(buffer.data(), buffer.size());

        const ssize_t n = io::recvmsg(fd, msg, 0);

        if (n > 0) {
            total_bytes += static_cast<std::uint64_t>(n);
            benchmark::DoNotOptimize(buffer.data());
            benchmark::DoNotOptimize(total_bytes);
            benchmark::ClobberMemory();
            continue;
        }

        if (n == 0) {
            return total_bytes > 0;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (!wait_for_readable(fd)) {
                return false;
            }
            continue;
        }

        return false;
    }
}

static bool run_benchmark_client(const char* ip,
                                 int port,
                                 std::array<char, BUFFER_SIZE>& buffer,
                                 std::uint64_t& total_bytes) {
    SocketHandle sock = connect_to_server(ip, port);
    if (native_fd(sock) < 0) {
        return false;
    }

    return receive_file(sock, buffer, total_bytes);
}

static void BM_TCP_FileDownload(benchmark::State& state) {
    constexpr const char* ip = "127.0.0.1";
    const int port = g_port;

    std::array<char, BUFFER_SIZE> buffer{};
    std::uint64_t bytes_processed = 0;
    std::uint64_t last_downloaded_bytes = 0;

    for (auto _ : state) {
        (void)_;

        std::uint64_t downloaded_bytes = 0;
        if (!run_benchmark_client(ip, port, buffer, downloaded_bytes)) {
            state.SkipWithError("Download failed.");
            break;
        }

        bytes_processed += downloaded_bytes;
        last_downloaded_bytes = downloaded_bytes;
    }

    state.SetBytesProcessed(static_cast<int64_t>(bytes_processed));
    state.counters["downloaded_bytes"] = static_cast<double>(last_downloaded_bytes);
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