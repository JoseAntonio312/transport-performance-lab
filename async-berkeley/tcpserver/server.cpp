/*
 * Copyright (c) 2026 Jose Antonio Garcia Montanez
 *
 * TCP file server with async-berkeley
 * Minimal raw-byte server for performance and energy measurements.
 */

#include <csignal>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <io/io.hpp>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using SocketHandle = io::socket::socket_handle;

constexpr int DEFAULT_PORT = 8080;
constexpr int BACKLOG = 128;
constexpr int MAX_THREADS = 256;
constexpr std::size_t MAX_ACTIVE_CLIENTS = 4096;

struct FileMapping {
    int fd = -1;
    const char* data = nullptr;
    std::size_t size = 0;
};

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

static FileMapping map_file_read_only(const fs::path& path) {
    FileMapping mapping{};

    const std::uintmax_t file_size = fs::file_size(path);
    mapping.size = static_cast<std::size_t>(file_size);

    mapping.fd = open(path.c_str(), O_RDONLY);
    if (mapping.fd == -1) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }

    if (mapping.size == 0) {
        mapping.data = nullptr;
        return mapping;
    }

    void* ptr = mmap(nullptr, mapping.size, PROT_READ, MAP_PRIVATE, mapping.fd, 0);
    if (ptr == MAP_FAILED) {
        close(mapping.fd);
        throw std::runtime_error("mmap() failed.");
    }

    mapping.data = static_cast<const char*>(ptr);
    return mapping;
}

static void unmap_file(FileMapping& mapping) {
    if (mapping.data != nullptr && mapping.size > 0) {
        munmap(const_cast<char*>(mapping.data), mapping.size);
    }

    if (mapping.fd != -1) {
        close(mapping.fd);
    }

    mapping.fd = -1;
    mapping.data = nullptr;
    mapping.size = 0;
}

// Try exactly one non-blocking sendmsg() step for one client.
// Return values:
//   1 -> full transfer completed
//   0 -> partial progress or would block; continue later
//  -1 -> fatal error
static int send_file_step(SocketHandle& fd, std::size_t& sent, std::span<const char> payload) {
    if (sent >= payload.size()) {
        return 1;
    }

    io::socket::socket_message<> msg;
    msg.buffers.emplace_back(
        const_cast<char*>(payload.data() + sent),
        payload.size() - sent
    );

    const ssize_t n = io::sendmsg(fd, msg, 0);

    if (n > 0) {
        sent += static_cast<std::size_t>(n);
        return sent == payload.size() ? 1 : 0;
    }

    if (n == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;
    }

    return -1;
}

static int serve_client_step(SocketHandle& fd, std::size_t& sent, std::span<const char> payload) {
    return send_file_step(fd, sent, payload);
}

static void accept_new_clients(
    SocketHandle& server_fd,
    std::array<SocketHandle, MAX_ACTIVE_CLIENTS>& client_fds,
    std::array<std::size_t, MAX_ACTIVE_CLIENTS>& client_sent,
    std::size_t& client_count
) {
    while (client_count < MAX_ACTIVE_CLIENTS) {
        auto [client_fd, client_addr] = io::accept(server_fd);
        (void)client_addr;

        if (native_fd(client_fd) < 0) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }

            return;
        }

        if (!set_nonblocking(client_fd)) {
            continue;
        }

        client_fds[client_count] = std::move(client_fd);
        client_sent[client_count] = 0;
        ++client_count;
    }
}

static void accept_loop(SocketHandle& server_fd, std::span<const char> payload) {
    std::array<SocketHandle, MAX_ACTIVE_CLIENTS> client_fds{};
    std::array<std::size_t, MAX_ACTIVE_CLIENTS> client_sent{};
    std::array<pollfd, MAX_ACTIVE_CLIENTS + 1> pollfds{};

    std::size_t client_count = 0;

    while (true) {
        pollfds[0].fd = native_fd(server_fd);
        pollfds[0].events = POLLIN;
        pollfds[0].revents = 0;

        for (std::size_t i = 0; i < client_count; ++i) {
            pollfds[i + 1].fd = native_fd(client_fds[i]);
            pollfds[i + 1].events = POLLOUT;
            pollfds[i + 1].revents = 0;
        }

        const int ready = poll(pollfds.data(), static_cast<nfds_t>(client_count + 1), -1);

        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (pollfds[0].revents & POLLIN) {
            accept_new_clients(server_fd, client_fds, client_sent, client_count);
        }

        std::size_t i = 0;
        while (i < client_count) {
            const short revents = pollfds[i + 1].revents;
            bool remove_client = false;

            if (revents & POLLOUT) {
                const int status = serve_client_step(client_fds[i], client_sent[i], payload);

                if (status == 1 || status == -1) {
                    client_fds[i] = {};
                    remove_client = true;
                }
            } else if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
                client_fds[i] = {};
                remove_client = true;
            }

            if (remove_client) {
                if (i + 1 < client_count) {
                    client_fds[i] = std::move(client_fds[client_count - 1]);
                    client_sent[i] = client_sent[client_count - 1];
                }
                --client_count;
            } else {
                ++i;
            }
        }
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGPIPE, SIG_IGN);

    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: " << argv[0] << " <file_path> [port] [threads]\n";
        return 1;
    }

    const fs::path file_path = argv[1];
    int port = DEFAULT_PORT;
    int threads = 1;

    if (argc >= 3) {
        port = std::stoi(argv[2]);
        if (port <= 0 || port > 65535) {
            std::cerr << "Invalid port.\n";
            return 1;
        }
    }

    if (argc == 4) {
        threads = std::stoi(argv[3]);
        if (threads <= 0 || threads > MAX_THREADS) {
            std::cerr << "Invalid thread count.\n";
            return 1;
        }
    }

    if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
        std::cerr << "Input path is not a regular file: " << file_path << "\n";
        return 1;
    }

    FileMapping mapping{};
    try {
        mapping = map_file_read_only(file_path);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    const std::span<const char> payload(mapping.data, mapping.size);

    SocketHandle server_fd(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (native_fd(server_fd) < 0) {
        std::cerr << "socket() failed.\n";
        unmap_file(mapping);
        return 1;
    }

    int opt = 1;
    const auto opt_bytes = std::as_bytes(std::span{&opt, 1});
    if (io::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, opt_bytes) == -1) {
        std::cerr << "setsockopt() failed.\n";
        unmap_file(mapping);
        return 1;
    }

    if (!set_nonblocking(server_fd)) {
        std::cerr << "fcntl(server_fd) failed.\n";
        unmap_file(mapping);
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    const auto addr_bytes = std::as_bytes(std::span{&addr, 1});

    if (io::bind(server_fd, addr_bytes) == -1) {
        std::cerr << "bind() failed.\n";
        unmap_file(mapping);
        return 1;
    }

    if (io::listen(server_fd, BACKLOG) == -1) {
        std::cerr << "listen() failed.\n";
        unmap_file(mapping);
        return 1;
    }

    std::array<std::thread, MAX_THREADS> pool{};

    for (int i = 0; i < threads; ++i) {
        pool[static_cast<std::size_t>(i)] = std::thread(
            accept_loop,
            std::ref(server_fd),
            payload
        );
    }

    for (int i = 0; i < threads; ++i) {
        pool[static_cast<std::size_t>(i)].join();
    }

    unmap_file(mapping);
    return 0;
}