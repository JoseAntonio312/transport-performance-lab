/*
 * Copyright (c) 2026 Jose Antonio Garcia Montanez
 *
 * TCP file server with async-berkeley
 * Minimal raw-byte server for performance and energy measurements.
 */

#include <io/io.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace io;
using namespace io::socket;
using namespace io::execution;
using namespace stdexec;
using namespace exec;

using triggers = basic_triggers<poll_multiplexer>;
using dialog = socket_dialog<poll_multiplexer>;
using message = socket_message<sockaddr_in>;

namespace fs = std::filesystem;

constexpr int DEFAULT_PORT = 8080;
constexpr int BACKLOG = 128;
constexpr int MAX_THREADS = 256;

struct FileMapping {
    int fd = -1;
    const char* data = nullptr;
    std::size_t size = 0;
};

struct WriteState {
    dialog client;
    std::span<const char> payload;
    std::size_t sent = 0;

    WriteState(dialog&& accepted_client, std::span<const char> file_payload)
        : client(std::move(accepted_client)),
          payload(file_payload) {
    }
};

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
        ::close(mapping.fd);
        throw std::runtime_error("mmap failed.");
    }

    mapping.data = static_cast<const char*>(ptr);
    return mapping;
}

static void unmap_file(FileMapping& mapping) {
    if (mapping.data != nullptr && mapping.size > 0) {
        munmap(const_cast<char*>(mapping.data), mapping.size);
    }

    if (mapping.fd != -1) {
        ::close(mapping.fd);
    }

    mapping.data = nullptr;
    mapping.fd = -1;
    mapping.size = 0;
}

static constexpr auto error_handler = [](const auto& error) {
    if constexpr (std::is_same_v<std::decay_t<decltype(error)>, int>) {
        std::cerr << std::error_code(error, std::system_category()).message() << "\n";
    } else {
        std::cerr << "async operation failed\n";
    }
};

static void send_file(
    async_scope& scope,
    std::shared_ptr<WriteState> state
) {
    if (state->sent >= state->payload.size()) {
        return;
    }

    const std::size_t remaining = state->payload.size() - state->sent;

    auto msg = std::make_shared<message>();
    msg->buffers.emplace_back(
        const_cast<char*>(state->payload.data() + state->sent),
        remaining
    );

    auto operation =
        sendmsg(state->client, *msg, 0)
        | then([&scope, state, msg](ssize_t bytes_sent) {
            if (bytes_sent <= 0) {
                return;
            }

            state->sent += static_cast<std::size_t>(bytes_sent);
            send_file(scope, state);
        })
        | upon_error([state, msg](const auto& error) {
            error_handler(error);
        });

    scope.spawn(std::move(operation));
}

static void serve_client(
    async_scope& scope,
    dialog&& client,
    std::span<const char> payload
) {
    auto state = std::make_shared<WriteState>(
        std::move(client),
        payload
    );

    send_file(scope, state);
}

static void accept_loop(
    async_scope& scope,
    const dialog& server,
    std::span<const char> payload
) {
    auto operation =
        accept(server)
        | then([&scope, &server, payload](auto result) {
            auto [client, addr] = std::move(result);
            (void)addr;

            serve_client(scope, std::move(client), payload);
            accept_loop(scope, server, payload);
        })
        | upon_error(error_handler);

    scope.spawn(std::move(operation));
}

static void run_triggers(triggers& trigs) {
    while (trigs.wait()) {
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGPIPE, SIG_IGN);

    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: " << argv[0] << " <file_path> [port] [threads]\n";
        return EXIT_FAILURE;
    }

    const fs::path file_path = argv[1];
    int port = DEFAULT_PORT;
    int threads = 1;

    if (argc >= 3) {
        port = std::stoi(argv[2]);
        if (port <= 0 || port > 65535) {
            std::cerr << "Invalid port.\n";
            return EXIT_FAILURE;
        }
    }

    if (argc == 4) {
        threads = std::stoi(argv[3]);
        if (threads <= 0 || threads > MAX_THREADS) {
            std::cerr << "Invalid thread count.\n";
            return EXIT_FAILURE;
        }
    }

    if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
        std::cerr << "Input path is not a regular file: " << file_path << "\n";
        return EXIT_FAILURE;
    }

    FileMapping mapping{};

    try {
        mapping = map_file_read_only(file_path);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    const std::span<const char> payload(mapping.data, mapping.size);

    try {
        async_scope scope;
        triggers trigs;

        auto server = trigs.emplace(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        auto server_address = make_address<sockaddr_in>();
        server_address->sin_family = AF_INET;
        server_address->sin_addr.s_addr = htonl(INADDR_ANY);
        server_address->sin_port = htons(static_cast<std::uint16_t>(port));

        socket_option<int> reuse{1};
        if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reuse)) {
            throw std::system_error(
                {errno, std::system_category()},
                "setsockopt failed"
            );
        }

        if (::io::bind(server, server_address)) {
            throw std::system_error(
                {errno, std::system_category()},
                "bind failed"
            );
        }

        if (::io::listen(server, BACKLOG)) {
            throw std::system_error(
                {errno, std::system_category()},
                "listen failed"
            );
        }

        accept_loop(scope, server, payload);

        std::vector<std::thread> pool;
        pool.reserve(static_cast<std::size_t>(threads));

        for (int i = 0; i < threads; ++i) {
            pool.emplace_back(run_triggers, std::ref(trigs));
        }

        for (auto& worker : pool) {
            worker.join();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        unmap_file(mapping);
        return EXIT_FAILURE;
    }

    unmap_file(mapping);
    return EXIT_SUCCESS;
}