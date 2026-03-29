/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * TCP file client with poll()
 * Minimal output version for performance and energy measurements.
 */

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// Puerto por defecto
constexpr int DEFAULT_PORT = 8080;

// Buffer de recepcion
constexpr std::size_t BUFFER_SIZE = 8192;

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

// Recibir exactamente N bytes
static bool recv_exact(int fd, void* buffer, std::size_t total) {
    char* ptr = static_cast<char*>(buffer);
    std::size_t received = 0;

    while (received < total) {
        ssize_t n = recv(fd, ptr + received, total - received, 0);

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

            // Socket error
            return false;
        }
    }

    // End
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

int main(int argc, char* argv[]) {

    // Check args
    if (argc < 2 || argc > 4) {
        std::cerr << "Uso: " << argv[0] << " <ip_servidor> [puerto] [nombre_salida]\n";
        return 1;
    }

    std::string server_ip = argv[1];
    int port = DEFAULT_PORT;
    std::string custom_output_name;

    // Check port
    if (argc >= 3) {
        port = std::stoi(argv[2]);
        if (port <= 0 || port > 65535) {
            std::cerr << "Puerto invalido.\n";
            return 1;
        }
    }

    // Custom output
    if (argc == 4) {
        custom_output_name = argv[3];
    }

    // Socket creation
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        std::perror("socket");
        return 1;
    }

    // Socket non blocking
    if (!set_nonblocking(sock)) {
        std::perror("fcntl(sock)");
        close(sock);
        return 1;
    }

    // Socket config
    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(static_cast<uint16_t>(port));

    // IP conversion
    if (inet_pton(AF_INET, server_ip.c_str(), &server.sin_addr) <= 0) {
        std::cerr << "IP invalida: " << server_ip << "\n";
        close(sock);
        return 1;
    }

    // Connect
    if (connect(sock, reinterpret_cast<sockaddr*>(&server), sizeof(server)) == -1) {
        if (errno != EINPROGRESS) {
            std::perror("connect");
            close(sock);
            return 1;
        }

        if (!wait_for_connect(sock)) {
            std::perror("connect");
            close(sock);
            return 1;
        }
    }

    try {
        // Read filename size
        std::uint32_t filename_size = read_u32(sock);
        if (filename_size == 0 || filename_size > 4096) {
            throw std::runtime_error("Tamano de nombre invalido.");
        }

        // Read filename
        std::string filename(filename_size, '\0');
        if (!recv_exact(sock, filename.data(), filename_size)) {
            throw std::runtime_error("Error leyendo nombre de fichero.");
        }

        // Read file size
        std::uint64_t file_size = read_u64(sock);

        // Output filename
        const std::string& output_name = custom_output_name.empty() ? filename : custom_output_name;

        // Open output file
        std::ofstream out(output_name, std::ios::binary);
        if (!out) {
            throw std::runtime_error("No se pudo crear el fichero de salida: " + output_name);
        }

        // Reusable buffer
        std::vector<char> buffer(BUFFER_SIZE);
        std::uint64_t remaining = file_size;

        // Main receive loop
        while (remaining > 0) {
            std::size_t chunk = static_cast<std::size_t>(
                remaining > buffer.size() ? buffer.size() : remaining
            );

            ssize_t n = recv(sock, buffer.data(), chunk, 0);

            if (n > 0) {
                // Direct write to file
                out.write(buffer.data(), n);
                remaining -= static_cast<std::uint64_t>(n);
            } else if (n == 0) {
                // Unexpected close
                throw std::runtime_error("Conexion cerrada antes de terminar la descarga.");
            } else {
                // Interrupted syscall
                if (errno == EINTR) continue;

                // Waiting for data
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    pollfd pfd{};
                    pfd.fd = sock;
                    pfd.events = POLLIN;

                    while (true) {
                        int ready = poll(&pfd, 1, -1);

                        if (ready > 0) {
                            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                                throw std::runtime_error("Conexion cerrada antes de terminar la descarga.");
                            }

                            if (pfd.revents & POLLIN) {
                                break;
                            }
                        } else if (ready == -1) {
                            if (errno == EINTR) continue;
                            throw std::runtime_error(std::string("Error en poll: ") + std::strerror(errno));
                        }
                    }

                    continue;
                }

                // Socket error
                throw std::runtime_error(std::string("Error en recv: ") + std::strerror(errno));
            }
        }

        out.close();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        close(sock);
        return 1;
    }

    // Closing socket
    close(sock);
    return 0;
}