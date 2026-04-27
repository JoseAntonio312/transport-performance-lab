/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * Asyncronous BSD sockets client for file download benchmark with epoll()
 * Minimal raw-byte client for performance and energy measurements.
 */

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <cstdint>

// Default remote TCP port.
constexpr int DEFAULT_PORT = 8080;

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
static int connect_tcp(const std::string& server_ip, int port) {
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

    if (inet_pton(AF_INET, server_ip.c_str(), &server.sin_addr) <= 0) {
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

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: " << argv[0] << " <server_ip> [port] [output_file]\n";
        return 1;
    }

    const std::string server_ip = argv[1];
    int port = DEFAULT_PORT;
    std::string output_path = "downloaded.bin";

    if (argc >= 3) {
        port = std::stoi(argv[2]);
        if (port <= 0 || port > 65535) {
            std::cerr << "Invalid port.\n";
            return 1;
        }
    }

    if (argc == 4) {
        output_path = argv[3];
    }

    const int sock = connect_tcp(server_ip, port);
    if (sock == -1) {
        std::cerr << "connect failed\n";
        return 1;
    }

    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        std::cerr << "Failed to open output file: " << output_path << "\n";
        close(sock);
        return 1;
    }

    std::array<char, BUFFER_SIZE> buffer{};

    while (true) {
        const ssize_t n = recv(sock, buffer.data(), buffer.size(), 0);

        if (n > 0) {
            out.write(buffer.data(), static_cast<std::streamsize>(n));
            if (!out) {
                std::cerr << "Failed to write output file.\n";
                close(sock);
                return 1;
            }
            continue;
        }

        if (n == 0) {
            break;
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
                        std::cerr << "Connection closed before completion.\n";
                        close(sock);
                        return 1;
                    }

                    if (pfd.revents & POLLIN) {
                        break;
                    }
                } else if (ready == -1) {
                    if (errno == EINTR) {
                        continue;
                    }

                    std::cerr << "poll failed: " << std::strerror(errno) << "\n";
                    close(sock);
                    return 1;
                }
            }

            continue;
        }

        std::cerr << "recv failed: " << std::strerror(errno) << "\n";
        close(sock);
        return 1;
    }

    out.close();
    close(sock);
    return 0;
}