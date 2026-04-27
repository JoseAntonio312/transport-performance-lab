/*
 * Copyright (c) 2026 Jose Antonio Garcia Montanez
 *
 * TCP file client with async-berkeley
 * Minimal raw-byte client for performance and energy measurements.
 */

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
#include <cstring>
#include <fstream>
#include <iostream>
#include <span>
#include <string>

using SocketHandle = io::socket::socket_handle;

constexpr int DEFAULT_PORT = 8080;
constexpr std::size_t BUFFER_SIZE = 8192;

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

static SocketHandle connect_to_server(const std::string& server_ip, int port) {
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

    if (inet_pton(AF_INET, server_ip.c_str(), &server.sin_addr) <= 0) {
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

static bool receive_file(SocketHandle& fd, const std::string& output_path) {
    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        std::cerr << "Failed to open output file: " << output_path << "\n";
        return false;
    }

    std::array<char, BUFFER_SIZE> buffer{};

    while (true) {
        io::socket::socket_message<> msg;
        msg.buffers.emplace_back(buffer.data(), buffer.size());

        const ssize_t n = io::recvmsg(fd, msg, 0);

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
            if (!wait_for_readable(fd)) {
                std::cerr << "poll failed before transfer completion.\n";
                return false;
            }
            continue;
        }

        std::cerr << "recvmsg failed: " << std::strerror(errno) << "\n";
        return false;
    }

    out.close();
    return true;
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

    SocketHandle sock = connect_to_server(server_ip, port);
    if (native_fd(sock) < 0) {
        std::cerr << "connect failed\n";
        return 1;
    }

    return receive_file(sock, output_path) ? 0 : 1;
}