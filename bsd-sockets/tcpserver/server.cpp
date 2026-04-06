/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * TCP file server with poll()
 * Minimal output version for performance and energy measurements.
 */

#include <csignal>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Alias std::filesystem
namespace fs = std::filesystem;

// Puerto por defecto
constexpr int DEFAULT_PORT = 8080;

// Conexiones tope
constexpr int BACKLOG = 128;

// Estructura estado cliente
struct ClientState {
    int fd;               // socket descriptor
    std::size_t sent = 0; // sent bytes
};

struct WorkerState {
    int wake_pipe_read = -1;
    int wake_pipe_write = -1;
    std::mutex mutex;
    std::vector<int> pending_clients;
};

// Poner un socket en modo no bloqueante
static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

// Intentar enviar todo lo posible a un cliente
static int send_all_possible(int fd, std::size_t& sent, const std::vector<char>& packet) {
    while (sent < packet.size()) {
        ssize_t n = send(fd,
                         packet.data() + sent,
                         packet.size() - sent,
                         0);

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

// Manejo de la arquitectura de red
static void append_u32(std::vector<char>& buffer, std::uint32_t value) {
    std::uint32_t net = htonl(value);
    const char* p = reinterpret_cast<const char*>(&net);
    buffer.insert(buffer.end(), p, p + sizeof(net));
}

// Añadir un uint64_t al buffer en formato de red
static void append_u64(std::vector<char>& buffer, std::uint64_t value) {
    std::uint32_t high = htonl(static_cast<std::uint32_t>(value >> 32));
    std::uint32_t low  = htonl(static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));

    const char* p1 = reinterpret_cast<const char*>(&high);
    const char* p2 = reinterpret_cast<const char*>(&low);

    buffer.insert(buffer.end(), p1, p1 + sizeof(high));
    buffer.insert(buffer.end(), p2, p2 + sizeof(low));
}

// Cargar el fichero completo en memoria
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

// Construcción de nuestro paquete
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

static void worker_loop(WorkerState& worker, const std::vector<char>& packet) {
    std::vector<ClientState> clients;

    while (true) {
        std::vector<pollfd> pollfds;
        pollfds.reserve(1 + clients.size());

        pollfds.push_back({worker.wake_pipe_read, POLLIN, 0});

        for (const auto& client : clients) {
            pollfds.push_back({client.fd, POLLOUT, 0});
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

            std::vector<int> new_clients;
            {
                std::lock_guard<std::mutex> lock(worker.mutex);
                new_clients.swap(worker.pending_clients);
            }

            for (int fd : new_clients) {
                clients.push_back({fd, 0});
            }
        }

        for (std::size_t i = 0; i < clients.size();) {
            bool erase_client = false;
            short revents = pollfds[i + 1].revents;

            if (revents & POLLOUT) {
                std::size_t before = clients[i].sent;
                int status = send_all_possible(clients[i].fd, clients[i].sent, packet);

                if (status == 1) {
                    close(clients[i].fd);
                    erase_client = true;
                } else if (status == -1 && clients[i].sent == before) {
                    close(clients[i].fd);
                    erase_client = true;
                }
            } else if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
                close(clients[i].fd);
                erase_client = true;
            }

            if (erase_client) {
                clients.erase(clients.begin() + static_cast<long>(i));
            } else {
                ++i;
            }
        }
    }

    for (auto& c : clients) {
        close(c.fd);
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
    const std::vector<char> packet = build_packet(filename, file_data);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::perror("socket");
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        std::perror("setsockopt");
        close(server_fd);
        return 1;
    }

    if (!set_nonblocking(server_fd)) {
        std::perror("fcntl(server_fd)");
        close(server_fd);
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        std::perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, BACKLOG) == -1) {
        std::perror("listen");
        close(server_fd);
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
            close(server_fd);
            return 1;
        }

        workers[static_cast<std::size_t>(i)].wake_pipe_read = pipefd[0];
        workers[static_cast<std::size_t>(i)].wake_pipe_write = pipefd[1];

        set_nonblocking(pipefd[0]);
        set_nonblocking(pipefd[1]);

        pool.emplace_back(worker_loop,
                          std::ref(workers[static_cast<std::size_t>(i)]),
                          std::cref(packet));
    }

    std::size_t next_worker = 0;

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);

        if (client_fd == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                pollfd pfd{server_fd, POLLIN, 0};
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
            close(client_fd);
            continue;
        }

        WorkerState& worker = workers[next_worker];
        next_worker = (next_worker + 1U) % workers.size();

        {
            std::lock_guard<std::mutex> lock(worker.mutex);
            worker.pending_clients.push_back(client_fd);
        }

        const char wake = 1;
        (void)write(worker.wake_pipe_write, &wake, sizeof(wake));
    }

    close(server_fd);

    for (auto& worker : workers) {
        close(worker.wake_pipe_read);
        close(worker.wake_pipe_write);
    }

    for (auto& t : pool) {
        t.join();
    }

    return 0;
}