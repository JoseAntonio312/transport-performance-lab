/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * BSD sockets file download benchmark with poll()
 * Minimal output version for performance and future energy measurements.
 */

#include <benchmark/benchmark.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

// Puerto del servidor
constexpr int DEFAULT_PORT = 8080;

// Puerto global configurable
static int g_port = DEFAULT_PORT;

// Poner un socket en modo no bloqueante
static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

// Esperar a que el socket conecte
static bool wait_for_connect(int fd) {
    pollfd pfd{};
    pfd.fd = fd;
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

                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) == -1) {
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
static int connect_tcp(const char* ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        return -1;
    }

    if (!set_nonblocking(sock)) {
        close(sock);
        return -1;
    }

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(static_cast<uint16_t>(port));

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

// Recibir exactamente N bytes
static bool recv_exact(int fd, void* buffer, std::size_t total) {
    char* ptr = static_cast<char*>(buffer);
    std::size_t received = 0;

    while (received < total) {
        ssize_t n = recv(fd, ptr + received, total - received, 0);

        if (n > 0) {
            received += static_cast<std::size_t>(n);
        } else if (n == 0) {
            return false;
        } else {
            if (errno == EINTR) continue;

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                pollfd pfd{};
                pfd.fd = fd;
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

            return false;
        }
    }

    return true;
}

// Lectura uint32_t en red
static std::uint32_t read_u32(int fd) {
    std::uint32_t net = 0;

    if (!recv_exact(fd, &net, sizeof(net))) {
        throw std::runtime_error("Error leyendo uint32.");
    }

    return ntohl(net);
}

// Lectura uint64_t en red
static std::uint64_t read_u64(int fd) {
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
    const int port = g_port;
    constexpr std::size_t BUFFER_SIZE = 8192;

    std::vector<char> buffer(BUFFER_SIZE);

    for (auto _ : state) {
        int sock = connect_tcp(ip, port);

        if (sock == -1) {
            state.SkipWithError("Error en connect()");
            break;
        }

        try {
            std::uint32_t filename_size = read_u32(sock);
            if (filename_size == 0 || filename_size > 4096) {
                close(sock);
                state.SkipWithError("Tamano de nombre invalido.");
                break;
            }

            std::string filename(filename_size, '\0');
            if (!recv_exact(sock, filename.data(), filename_size)) {
                close(sock);
                state.SkipWithError("Error leyendo nombre.");
                break;
            }

            std::uint64_t file_size = read_u64(sock);
            std::uint64_t remaining = file_size;

            while (remaining > 0) {
                std::size_t chunk = static_cast<std::size_t>(
                    remaining > buffer.size() ? buffer.size() : remaining
                );

                ssize_t n = recv(sock, buffer.data(), chunk, 0);

                if (n > 0) {
                    remaining -= static_cast<std::uint64_t>(n);
                } else if (n == 0) {
                    close(sock);
                    state.SkipWithError("Conexion cerrada antes de tiempo.");
                    return;
                } else {
                    if (errno == EINTR) continue;

                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        pollfd pfd{};
                        pfd.fd = sock;
                        pfd.events = POLLIN;

                        while (true) {
                            int ready = poll(&pfd, 1, -1);

                            if (ready > 0) {
                                if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                                    close(sock);
                                    state.SkipWithError("Conexion cerrada antes de tiempo.");
                                    return;
                                }

                                if (pfd.revents & POLLIN) {
                                    break;
                                }
                            } else if (ready == -1) {
                                if (errno == EINTR) continue;

                                close(sock);
                                state.SkipWithError("Error en poll().");
                                return;
                            }
                        }

                        continue;
                    }

                    close(sock);
                    state.SkipWithError("Error en recv().");
                    return;
                }
            }

            benchmark::DoNotOptimize(buffer.data());
            benchmark::ClobberMemory();

        } catch (...) {
            close(sock);
            state.SkipWithError("Error en descarga.");
            break;
        }

        close(sock);
    }
}

BENCHMARK(BM_TCP_FileDownload)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(1)
    ->UseRealTime();

int main(int argc, char** argv) {
    std::vector<char*> filtered_argv;
    filtered_argv.reserve(static_cast<std::size_t>(argc));
    filtered_argv.push_back(argv[0]);

    const std::string prefix = "--server_port=";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg.rfind(prefix, 0) == 0) {
            g_port = std::stoi(arg.substr(prefix.size()));
        } else {
            filtered_argv.push_back(argv[i]);
        }
    }

    int filtered_argc = static_cast<int>(filtered_argv.size());
    filtered_argv.push_back(nullptr);

    benchmark::Initialize(&filtered_argc, filtered_argv.data());
    if (benchmark::ReportUnrecognizedArguments(filtered_argc, filtered_argv.data())) {
        return 1;
    }

    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}