/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * TCP file server with poll()
 * Minimal raw-byte server for performance and energy measurements.
 */

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <thread>

namespace fs = std::filesystem;

// Default TCP listening port.
constexpr int DEFAULT_PORT = 8080;

// Maximum pending connections in the listen queue.
constexpr int BACKLOG = 128;

// Maximum number of simultaneously tracked clients per worker thread.
constexpr std::size_t MAX_CLIENTS_PER_WORKER = 256;

// Maximum supported worker threads.
constexpr int MAX_WORKER_THREADS = 256;

// Read-only file mapping.
struct FileMapping {
    int fd = -1;
    const char* data = nullptr;
    std::size_t size = 0;
};

// Set a file descriptor to non-blocking mode.
static bool set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

// Open the file and expose it as a read-only memory mapping.
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
        throw std::runtime_error("mmap failed.");
    }

    mapping.data = static_cast<const char*>(ptr);
    return mapping;
}

// Release a file mapping created by map_file_read_only().
static void unmap_file(FileMapping& mapping) {
    if (mapping.data != nullptr && mapping.size > 0) {
        munmap(const_cast<char*>(mapping.data), mapping.size);
    }

    if (mapping.fd != -1) {
        close(mapping.fd);
    }

    mapping.data = nullptr;
    mapping.fd = -1;
    mapping.size = 0;
}

// Try to send as much as possible to one client.
// Return values:
//   1  -> all bytes sent
//   0  -> partial progress, try again later
//  -1  -> socket error / closed
static int send_all_possible(int fd, std::size_t& sent, std::span<const char> payload) {
    while (sent < payload.size()) {
        const ssize_t n = send(fd, payload.data() + sent, payload.size() - sent, 0);

        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }

        if (n == 0) {
            return -1;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }

        return -1;
    }

    return 1;
}

// Remove one client by swapping it with the last active slot.
static void remove_client(std::array<int, MAX_CLIENTS_PER_WORKER>& client_fds,
                          std::array<std::size_t, MAX_CLIENTS_PER_WORKER>& client_sent,
                          std::size_t& client_count,
                          std::size_t index) {
    close(client_fds[index]);

    if (index + 1 != client_count) {
        client_fds[index] = client_fds[client_count - 1];
        client_sent[index] = client_sent[client_count - 1];
    }

    client_fds[client_count - 1] = -1;
    client_sent[client_count - 1] = 0;
    --client_count;
}

// One worker thread:
// - accepts new clients from the shared listening socket,
// - sends the mapped payload to its own client set,
// - closes clients when done or on error.
static void worker_loop(int listen_fd, std::span<const char> payload) {
    std::array<int, MAX_CLIENTS_PER_WORKER> client_fds{};
    std::array<std::size_t, MAX_CLIENTS_PER_WORKER> client_sent{};
    std::array<pollfd, 1 + MAX_CLIENTS_PER_WORKER> pollfds{};
    std::size_t client_count = 0;

    client_fds.fill(-1);
    client_sent.fill(0);

    while (true) {
        pollfds[0].fd = listen_fd;
        pollfds[0].events = POLLIN;
        pollfds[0].revents = 0;

        for (std::size_t i = 0; i < client_count; ++i) {
            pollfds[i + 1].fd = client_fds[i];
            pollfds[i + 1].events = POLLOUT;
            pollfds[i + 1].revents = 0;
        }

        const int ready = poll(pollfds.data(), static_cast<nfds_t>(1 + client_count), -1);

        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        // Accept as many queued connections as possible in this thread.
        if (pollfds[0].revents & POLLIN) {
            while (true) {
                const int client_fd = accept(listen_fd, nullptr, nullptr);

                if (client_fd == -1) {
                    if (errno == EINTR) {
                        continue;
                    }

                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }

                    break;
                }

                if (!set_nonblocking(client_fd)) {
                    close(client_fd);
                    continue;
                }

                if (client_count >= MAX_CLIENTS_PER_WORKER) {
                    close(client_fd);
                    continue;
                }

                client_fds[client_count] = client_fd;
                client_sent[client_count] = 0;
                ++client_count;
            }
        }

        // Process writable clients.
        std::size_t i = 0;
        while (i < client_count) {
            const short revents = pollfds[i + 1].revents;

            if (revents & POLLOUT) {
                const int status = send_all_possible(client_fds[i], client_sent[i], payload);

                if (status == 1 || status == -1) {
                    remove_client(client_fds, client_sent, client_count, i);
                    continue;
                }
            } else if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
                remove_client(client_fds, client_sent, client_count, i);
                continue;
            }

            ++i;
        }
    }

    for (std::size_t i = 0; i < client_count; ++i) {
        close(client_fds[i]);
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
        if (threads <= 0 || threads > MAX_WORKER_THREADS) {
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

    const int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        std::perror("socket");
        unmap_file(mapping);
        return 1;
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        std::perror("setsockopt");
        close(listen_fd);
        unmap_file(mapping);
        return 1;
    }

    if (!set_nonblocking(listen_fd)) {
        std::perror("fcntl");
        close(listen_fd);
        unmap_file(mapping);
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        std::perror("bind");
        close(listen_fd);
        unmap_file(mapping);
        return 1;
    }

    if (listen(listen_fd, BACKLOG) == -1) {
        std::perror("listen");
        close(listen_fd);
        unmap_file(mapping);
        return 1;
    }

    std::array<std::thread, MAX_WORKER_THREADS> workers{};

    for (int i = 0; i < threads; ++i) {
        workers[static_cast<std::size_t>(i)] = std::thread(worker_loop, listen_fd, payload);
    }

    for (int i = 0; i < threads; ++i) {
        workers[static_cast<std::size_t>(i)].join();
    }

    close(listen_fd);
    unmap_file(mapping);
    return 0;
}