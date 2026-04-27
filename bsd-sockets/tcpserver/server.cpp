/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * TCP file server with epoll()
 * Asynchronous BSD sockets server for performance and energy measurements.
 */

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
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
#include <stdexcept>
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

// Maximum number of events returned by epoll_wait().
constexpr int MAX_EPOLL_EVENTS = 256;

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
        const ssize_t n = send(fd, payload.data() + sent, payload.size() - sent, MSG_NOSIGNAL);

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

// Find one client inside the fixed client array.
static std::size_t find_client_index(
    const std::array<int, MAX_CLIENTS_PER_WORKER>& client_fds,
    std::size_t client_count,
    int fd
) {
    for (std::size_t i = 0; i < client_count; ++i) {
        if (client_fds[i] == fd) {
            return i;
        }
    }

    return client_count;
}

// Remove one client by swapping it with the last active slot.
static void remove_client(std::array<int, MAX_CLIENTS_PER_WORKER>& client_fds,
                          std::array<std::size_t, MAX_CLIENTS_PER_WORKER>& client_sent,
                          std::size_t& client_count,
                          std::size_t index,
                          int epoll_fd) {
    const int fd = client_fds[index];

    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);

    if (index + 1 != client_count) {
        client_fds[index] = client_fds[client_count - 1];
        client_sent[index] = client_sent[client_count - 1];
    }

    client_fds[client_count - 1] = -1;
    client_sent[client_count - 1] = 0;
    --client_count;
}

// One asynchronous worker thread:
// - accepts new clients from the shared listening socket,
// - registers each client in epoll,
// - sends the mapped payload only when a socket is writable,
// - closes clients when done or on error.
static void worker_loop(int listen_fd, std::span<const char> payload) {
    std::array<int, MAX_CLIENTS_PER_WORKER> client_fds{};
    std::array<std::size_t, MAX_CLIENTS_PER_WORKER> client_sent{};
    std::array<epoll_event, MAX_EPOLL_EVENTS> events{};

    std::size_t client_count = 0;

    client_fds.fill(-1);
    client_sent.fill(0);

    const int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        std::cerr << "epoll_create1 failed: " << std::strerror(errno) << "\n";
        return;
    }

    epoll_event listen_event{};
    listen_event.events = EPOLLIN;
    listen_event.data.fd = listen_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &listen_event) == -1) {
        std::cerr << "epoll_ctl listen_fd failed: " << std::strerror(errno) << "\n";
        close(epoll_fd);
        return;
    }

    while (true) {
        const int ready = epoll_wait(epoll_fd, events.data(), MAX_EPOLL_EVENTS, -1);

        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }

            std::cerr << "epoll_wait failed: " << std::strerror(errno) << "\n";
            break;
        }

        for (int event_index = 0; event_index < ready; ++event_index) {
            const int fd = events[static_cast<std::size_t>(event_index)].data.fd;
            const std::uint32_t revents = events[static_cast<std::size_t>(event_index)].events;

            if (fd == listen_fd) {
                while (client_count < MAX_CLIENTS_PER_WORKER) {
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

                    epoll_event client_event{};
                    client_event.events = EPOLLOUT | EPOLLERR | EPOLLHUP;
                    client_event.data.fd = client_fd;

                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event) == -1) {
                        close(client_fd);
                        continue;
                    }

                    client_fds[client_count] = client_fd;
                    client_sent[client_count] = 0;
                    ++client_count;
                }

                continue;
            }

            const std::size_t index = find_client_index(client_fds, client_count, fd);
            if (index == client_count) {
                continue;
            }

            if (revents & (EPOLLERR | EPOLLHUP)) {
                remove_client(client_fds, client_sent, client_count, index, epoll_fd);
                continue;
            }

            if (revents & EPOLLOUT) {
                const int status = send_all_possible(fd, client_sent[index], payload);

                if (status == 1 || status == -1) {
                    remove_client(client_fds, client_sent, client_count, index, epoll_fd);
                    continue;
                }
            }
        }
    }

    for (std::size_t i = 0; i < client_count; ++i) {
        close(client_fds[i]);
    }

    close(epoll_fd);
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

#ifdef SO_REUSEPORT
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) == -1) {
        std::perror("setsockopt SO_REUSEPORT");
        close(listen_fd);
        unmap_file(mapping);
        return 1;
    }
#endif

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