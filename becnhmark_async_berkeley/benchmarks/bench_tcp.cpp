/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * async-berkeley file download benchmark with poll()
 * Minimal output version for performance and future energy measurements.
 */

#include <benchmark/benchmark.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <unistd.h>

#include <io/io.hpp>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// Alias async-berkeley socket handle
using SocketHandle = io::socket::socket_handle;

// Puerto del servidor
constexpr int DEFAULT_PORT = 8080;

// Poner un socket en modo no bloqueante
static bool set_nonblocking(SocketHandle& fd) {
    int flags = io::fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return io::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

// Obtener el descriptor nativo para poll()
// Este helper existe solo para mantener poll() exactamente igual
// que en la version BSD y asi conservar comparabilidad experimental.
static int native_fd(const SocketHandle& fd) {
    return static_cast<int>(fd);
}

// Esperar a que el socket conecte
static bool wait_for_connect(SocketHandle& fd) {
    pollfd pfd{};
    pfd.fd = native_fd(fd);
    pfd.events = POLLOUT;

    while (true) {
        int ready = poll(&pfd, 1, -1);

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
            if (errno == EINTR) continue;
            return false;
        }
    }
}

// Conexion TCP basica
static SocketHandle connect_tcp(const char* ip, int port) {
    // Socket creation
    // IMPORTANTE:
    // Aqui ya no usamos socket() de BSD directamente.
    // El benchmark usa async-berkeley mediante socket_handle.
    SocketHandle sock(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    // Socket non blocking
    if (!set_nonblocking(sock)) {
        return {};
    }

    // Socket config
    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(static_cast<uint16_t>(port));

    // IP conversion
    if (inet_pton(AF_INET, ip, &server.sin_addr) <= 0) {
        return {};
    }

    // Connect
    auto server_bytes = std::as_bytes(std::span{&server, 1});
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

// Recibir exactamente N bytes
static bool recv_exact(SocketHandle& fd, void* buffer, std::size_t total) {
    char* ptr = static_cast<char*>(buffer);
    std::size_t received = 0;

    while (received < total) {
        io::socket::socket_message<> msg;
        msg.buffers.emplace_back(ptr + received, total - received);

        ssize_t n = io::recvmsg(fd, msg, 0);

        if (n > 0) {
            // Counter update
            received += static_cast<std::size_t>(n);
        } else if (n == 0) {
            // Closed connection
            return false;
        } else {
            // Interrupted syscall
            if (errno == EINTR) continue;

            // Waiting for data
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                pollfd pfd{};
                pfd.fd = native_fd(fd);
                pfd.events = POLLIN;

                while (true) {
                    int ready = poll(&pfd, 1, -1);

                    if (ready > 0) {
                        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                            return false;
                        }

                        if (pfd.revents & POLLIN) {
                            break;
                        }
                    } else if (ready == -1) {
                        if (errno == EINTR) continue;
                        return false;
                    }
                }

                continue;
            }

            // Socket error
            return false;
        }
    }

    // End
    return true;
}

// Lectura uint32_t en red
static std::uint32_t read_u32(SocketHandle& fd) {
    std::uint32_t net = 0;

    if (!recv_exact(fd, &net, sizeof(net))) {
        throw std::runtime_error("Error leyendo uint32.");
    }

    return ntohl(net);
}

// Lectura uint64_t en red
static std::uint64_t read_u64(SocketHandle& fd) {
    std::uint32_t high_net = 0;
    std::uint32_t low_net = 0;

    if (!recv_exact(fd, &high_net, sizeof(high_net)) ||
        !recv_exact(fd, &low_net, sizeof(low_net))) {
        throw std::runtime_error("Error leyendo uint64.");
    }

    std::uint64_t high = ntohl(high_net);
    std::uint64_t low = ntohl(low_net);

    return (high << 32) | low;
}

// Benchmark de descarga del fichero servido
// Protocolo:
// [u32 tam_nombre][nombre][u64 tam_fichero][contenido]
static void BM_TCP_FileDownload(benchmark::State& state) {
    const char* ip = "127.0.0.1";
    const int port = DEFAULT_PORT;
    constexpr std::size_t BUFFER_SIZE = 8192;

    // Buffer reutilizable por hilo
    std::vector<char> buffer(BUFFER_SIZE);

    for (auto _ : state) {
        SocketHandle sock = connect_tcp(ip, port);

        if (native_fd(sock) < 0) {
            state.SkipWithError("Error en connect()");
            break;
        }

        try {
            // Read filename size
            std::uint32_t filename_size = read_u32(sock);
            if (filename_size == 0 || filename_size > 4096) {
                state.SkipWithError("Tamano de nombre invalido.");
                break;
            }

            // Read filename
            std::string filename(filename_size, '\0');
            if (!recv_exact(sock, filename.data(), filename_size)) {
                state.SkipWithError("Error leyendo nombre.");
                break;
            }

            // Read file size
            std::uint64_t file_size = read_u64(sock);
            std::uint64_t remaining = file_size;

            // Read content
            while (remaining > 0) {
                std::size_t chunk = static_cast<std::size_t>(
                    remaining > buffer.size() ? buffer.size() : remaining
                );

                io::socket::socket_message<> msg;
                msg.buffers.emplace_back(buffer.data(), chunk);

                ssize_t n = io::recvmsg(sock, msg, 0);

                if (n > 0) {
                    remaining -= static_cast<std::uint64_t>(n);
                } else if (n == 0) {
                    state.SkipWithError("Conexion cerrada antes de tiempo.");
                    return;
                } else {
                    if (errno == EINTR) continue;

                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        pollfd pfd{};
                        pfd.fd = native_fd(sock);
                        pfd.events = POLLIN;

                        while (true) {
                            int ready = poll(&pfd, 1, -1);

                            if (ready > 0) {
                                if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                                    state.SkipWithError("Conexion cerrada antes de tiempo.");
                                    return;
                                }

                                if (pfd.revents & POLLIN) {
                                    break;
                                }
                            } else if (ready == -1) {
                                if (errno == EINTR) continue;

                                state.SkipWithError("Error en poll().");
                                return;
                            }
                        }

                        continue;
                    }

                    state.SkipWithError("Error en recvmsg().");
                    return;
                }
            }

            benchmark::DoNotOptimize(buffer.data());
            benchmark::ClobberMemory();

        } catch (...) {
            state.SkipWithError("Error en descarga.");
            break;
        }
    }
}

BENCHMARK(BM_TCP_FileDownload)
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();