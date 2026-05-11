/*
 * Copyright (c) 2026 Jose Antonio Garcia Montanez
 *
 * TCP file client with BSD sockets
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
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

constexpr int DEFAULT_PORT = 8080;
constexpr std::size_t BUFFER_SIZE = 8192;

static bool set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

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

static int connect_to_server(const std::string& server_ip, int port) {
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

static bool wait_for_readable(int fd) {
    pollfd pfd{};
    pfd.fd = fd;
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

static bool receive_file(int sock, const std::string& output_path) {
    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        std::cerr << "Failed to open output file: " << output_path << "\n";
        return false;
    }

    std::array<char, BUFFER_SIZE> buffer{};

    while (true) {
        const ssize_t n = recv(sock, buffer.data(), buffer.size(), 0);

        if (n > 0) {
            out.write(buffer.data(), static_cast<std::streamsize>(n));
            if (!out) {
                std::cerr << "Failed to write output file.\n";
                return false;
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
            if (!wait_for_readable(sock)) {
                std::cerr << "poll failed before transfer completion.\n";
                return false;
            }
            continue;
        }

        std::cerr << "recv failed: " << std::strerror(errno) << "\n";
        return false;
    }

    out.close();
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: " << argv[0] << " <server_ip> [port] [output_file]\n";
        return EXIT_FAILURE;
    }

    const std::string server_ip = argv[1];
    int port = DEFAULT_PORT;
    std::string output_path = "downloaded.bin";

    if (argc >= 3) {
        port = std::stoi(argv[2]);
        if (port <= 0 || port > 65535) {
            std::cerr << "Invalid port.\n";
            return EXIT_FAILURE;
        }
    }

    if (argc == 4) {
        output_path = argv[3];
    }

    const int sock = connect_to_server(server_ip, port);
    if (sock == -1) {
        std::cerr << "connect failed\n";
        return EXIT_FAILURE;
    }

    const bool ok = receive_file(sock, output_path);

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}