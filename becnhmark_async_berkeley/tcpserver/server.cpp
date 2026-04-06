/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * TCP file server with async-berkeley
 * Minimal output version for performance and energy measurements.
 */

#include <csignal>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <unistd.h>

#include <io/io.hpp>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Alias std::filesystem
namespace fs = std::filesystem;

// Alias async-berkeley socket handle
using SocketHandle = io::socket::socket_handle;

// Puerto por defecto
constexpr int DEFAULT_PORT = 8080;

// Conexiones tope
constexpr int BACKLOG = 128;

struct ClientState {
    SocketHandle fd;
    std::size_t sent = 0;
};

struct WorkerState {
    int wake_pipe_read = -1;
    int wake_pipe_write = -1;
    std::mutex mutex;
    std::vector<SocketHandle> pending_clients;
};

static bool set_nonblocking(SocketHandle& fd) {
    int flags = io::fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return io::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

static int native_fd(const SocketHandle& fd) {
    return static_cast<int>(fd);
}

static int send_all_possible(io::socket::socket_handle& fd,
                             std::size_t& sent,
                             std::vector<char>& packet) {
    while (sent < packet.size()) {
        io::socket::socket_message<> msg;
        msg.buffers.emplace_back(
            static_cast<void*>(packet.data() + sent),
            packet.size() - sent
        );

        ssize_t n = io::sendmsg(fd, msg, 0);

        if (n > 0) {
            sent += static_cast<std::size_t>(n);
        } else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        } else {
            return -1;
        }
    }

    return 1;
}

static void append_u32(std::vector<char>& buffer, std::uint32_t value) {
    std::uint32_t net = htonl(value);
    const char* p = reinterpret_cast<const char*>(&net);
    buffer.insert(buffer.end(), p, p + sizeof(net));
}

static void append_u64(std::vector<char>& buffer, std::uint64_t value) {
    std::uint32_t high = htonl(static_cast<std::uint32_t>(value >> 32));
    std::uint32_t low  = htonl(static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));

    const char* p1 = reinterpret_cast<const char*>(&high);
    const char* p2 = reinterpret_cast<const char*>(&low);

    buffer.insert(buffer.end(), p1, p1 + sizeof(high));
    buffer.insert(buffer.end(), p2, p2 + sizeof(low));
}

static std::vector<char> load_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("No se pudo abrir el fichero: " + path.string());
    }

    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size < 0) {
        throw std::runtime_error("No se pudo obtener el tamano del fichero.");
    }

    std::vector<char> data(static_cast<std::size_t>(size));

    if (size > 0 && !file.read(data.data(), size)) {
        throw std::runtime_error("Error leyendo el fichero.");
    }

    return data;
}

static std::vector<char> build_packet(const std::string& filename, const std::vector<char>& file_data) {
    std::vector<char> packet;
    packet.reserve(sizeof(std::uint32_t) + filename.size() +
                   sizeof(std::uint64_t) + file_data.size());

    append_u32(packet, static_cast<std::uint32_t>(filename.size()));
    packet.insert(packet.end(), filename.begin(), filename.end());

    append_u64(packet, static_cast<std::uint64_t>(file_data.size()));
    packet.insert(packet.end(), file_data.begin(), file_data.end());

    return packet;
}

static void worker_loop(WorkerState& worker, std::vector<char>& packet) {
    std::vector<ClientState> clients;

    while (true) {
        std::vector<pollfd> pollfds;
        pollfds.reserve(1 + clients.size());

        pollfds.push_back({worker.wake_pipe_read, POLLIN, 0});

        for (const auto& client : clients) {
            pollfds.push_back({native_fd(client.fd), POLLOUT, 0});
        }

        int ready = poll(pollfds.data(),
                         static_cast<nfds_t>(pollfds.size()),
                         -1);

        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (pollfds[0].revents & POLLIN) {
            char tmp[64];
            while (read(worker.wake_pipe_read, tmp, sizeof(tmp)) > 0) {
            }

            std::vector<SocketHandle> new_clients;
            {
                std::lock_guard<std::mutex> lock(worker.mutex);
                new_clients.swap(worker.pending_clients);
            }

            for (auto& fd : new_clients) {
                clients.push_back({std::move(fd), 0});
            }
        }

        for (std::size_t i = 0; i < clients.size();) {
            bool erase_client = false;
            short revents = pollfds[i + 1].revents;

            if (revents & POLLOUT) {
                std::size_t before = clients[i].sent;
                int status = send_all_possible(clients[i].fd, clients[i].sent, packet);

                if (status == 1) {
                    clients[i].fd = {};
                    erase_client = true;
                } else if (status == -1 && clients[i].sent == before) {
                    clients[i].fd = {};
                    erase_client = true;
                }
            } else if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
                clients[i].fd = {};
                erase_client = true;
            }

            if (erase_client) {
                clients.erase(clients.begin() + static_cast<long>(i));
            } else {
                ++i;
            }
        }
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGPIPE, SIG_IGN);

    if (argc < 2 || argc > 4) {
        std::cerr << "Uso: " << argv[0] << " <ruta_fichero> [puerto] [num_hebras]\n";
        return 1;
    }

    const fs::path file_path = argv[1];
    int port = DEFAULT_PORT;
    int threads = 1;

    if (argc >= 3) {
        port = std::stoi(argv[2]);
        if (port <= 0 || port > 65535) {
            std::cerr << "Puerto invalido.\n";
            return 1;
        }
    }

    if (argc == 4) {
        threads = std::stoi(argv[3]);
        if (threads <= 0) {
            std::cerr << "Numero de hebras invalido.\n";
            return 1;
        }
    }

    if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
        std::cerr << "El fichero no existe o no es regular: " << file_path << "\n";
        return 1;
    }

    std::vector<char> file_data;
    try {
        file_data = load_file(file_path);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    const std::string filename = file_path.filename().string();
    std::vector<char> packet = build_packet(filename, file_data);

    SocketHandle server_fd(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    int opt = 1;
    auto opt_bytes = std::as_bytes(std::span{&opt, 1});
    if (io::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, opt_bytes) == -1) {
        std::perror("setsockopt");
        return 1;
    }

    if (!set_nonblocking(server_fd)) {
        std::perror("fcntl(server_fd)");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    auto addr_bytes = std::as_bytes(std::span{&addr, 1});

    if (io::bind(server_fd, addr_bytes) == -1) {
        std::perror("bind");
        return 1;
    }

    if (io::listen(server_fd, BACKLOG) == -1) {
        std::perror("listen");
        return 1;
    }

    std::cout << "Server ready on port " << port
              << " with " << threads << " threads\n";

    std::vector<WorkerState> workers(static_cast<std::size_t>(threads));
    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(threads));

    for (int i = 0; i < threads; ++i) {
        int pipefd[2];
        if (pipe(pipefd) == -1) {
            std::perror("pipe");
            return 1;
        }

        workers[static_cast<std::size_t>(i)].wake_pipe_read = pipefd[0];
        workers[static_cast<std::size_t>(i)].wake_pipe_write = pipefd[1];

        set_nonblocking(reinterpret_cast<SocketHandle&>(workers[static_cast<std::size_t>(i)].wake_pipe_read));
        set_nonblocking(reinterpret_cast<SocketHandle&>(workers[static_cast<std::size_t>(i)].wake_pipe_write));

        pool.emplace_back(worker_loop,
                          std::ref(workers[static_cast<std::size_t>(i)]),
                          std::ref(packet));
    }

    std::size_t next_worker = 0;

    while (true) {
        auto [client_fd, client_addr] = io::accept(server_fd);
        (void)client_addr;

        if (client_fd == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                pollfd pfd{native_fd(server_fd), POLLIN, 0};
                if (poll(&pfd, 1, -1) == -1 && errno != EINTR) {
                    std::perror("poll");
                    break;
                }
                continue;
            }
            std::perror("accept");
            continue;
        }

        if (!set_nonblocking(client_fd)) {
            std::perror("fcntl(client_fd)");
            continue;
        }

        WorkerState& worker = workers[next_worker];
        next_worker = (next_worker + 1U) % workers.size();

        {
            std::lock_guard<std::mutex> lock(worker.mutex);
            worker.pending_clients.push_back(std::move(client_fd));
        }

        const char wake = 1;
        (void)write(worker.wake_pipe_write, &wake, sizeof(wake));
    }

    return 0;
}