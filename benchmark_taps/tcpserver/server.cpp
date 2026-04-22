/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * TCP file server with TAPS
 * Minimal raw-byte server for performance and energy measurements.
 */

#include <csignal>

#include "taps/taps_api.h"
#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_awaitable.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// Default TCP port.
constexpr int DEFAULT_PORT = 8080;

// Maximum supported file size loaded in memory.
// Adjust this if you need larger files.
constexpr std::size_t MAX_FILE_SIZE = 2ull * 1024ull * 1024ull * 1024ull;

// Fixed in-memory file buffer.
struct FileBuffer {
    std::vector<char> data;
};

// Load the whole file into memory before serving clients.
// This removes disk I/O from the measurement phase.
static FileBuffer load_file_into_memory(const fs::path& path) {
    FileBuffer file_buffer;

    const auto file_size = fs::file_size(path);
    if (file_size > MAX_FILE_SIZE) {
        throw std::runtime_error("File is larger than MAX_FILE_SIZE.");
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }

    file_buffer.data.resize(static_cast<std::size_t>(file_size));

    if (file_size > 0) {
        file.read(file_buffer.data.data(), static_cast<std::streamsize>(file_size));
        if (!file) {
            throw std::runtime_error("Failed to read file contents.");
        }
    }

    return file_buffer;
}

// Send the whole file as raw bytes through one TAPS connection.
// No application-level header is used.
// Important: no co_await is performed inside the catch block.
static asio::awaitable<void> do_write(
    std::unique_ptr<taps::Connection> connection,
    std::span<const char> file_view
) {
    bool must_close = true;

    try {
        if (!file_view.empty()) {
            std::vector<std::uint8_t> payload(
                reinterpret_cast<const std::uint8_t*>(file_view.data()),
                reinterpret_cast<const std::uint8_t*>(file_view.data() + file_view.size())
            );

            auto result = co_await connection->send(taps::Message(std::move(payload)));
            if (!result) {
                must_close = true;
            }
        }
    } catch (...) {
        must_close = true;
    }

    if (must_close) {
        auto close_result = co_await connection->close();
        (void)close_result;
    }

    co_return;
}

// Accept loop for incoming TAPS connections.
static asio::awaitable<void> do_accept(
    taps::Listener& listener,
    std::span<const char> file_view
) {
    auto executor = co_await asio::this_coro::executor;

    while (true) {
        auto accept_result = co_await listener.accept();

        if (!accept_result) {
            continue;
        }

        asio::co_spawn(
            executor,
            do_write(std::move(*accept_result), file_view),
            asio::detached
        );
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
        if (threads <= 0) {
            std::cerr << "Invalid thread count.\n";
            return 1;
        }
    }

    if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
        std::cerr << "Input path is not a regular file: " << file_path << "\n";
        return 1;
    }

    FileBuffer file_buffer;
    try {
        file_buffer = load_file_into_memory(file_path);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    const std::span<const char> file_view(file_buffer.data.data(), file_buffer.data.size());

    try {
        asio::io_context io_context;

        asio::co_spawn(
            io_context,
            [&io_context, port, file_view]() -> asio::awaitable<void> {
                taps::TransportServices ts(io_context);

                taps::TransportProperties props;
                props.set(taps::PropertyKey::RELIABILITY, taps::SelectionProperty::REQUIRE);
                props.set(taps::PropertyKey::PRESERVE_ORDER, taps::SelectionProperty::REQUIRE);

                auto listener_result = co_await ts.listen(
                    taps::LocalEndpoint{"0.0.0.0", static_cast<std::uint16_t>(port)},
                    std::move(props)
                );

                if (!listener_result) {
                    std::cerr << "listen setup failed: "
                              << listener_result.error().message() << "\n";
                    co_return;
                }

                auto listener = std::move(*listener_result);

                auto start_result = co_await listener->listen();
                if (!start_result) {
                    std::cerr << "listen failed: "
                              << start_result.error().message() << "\n";
                    co_return;
                }

                co_await do_accept(*listener, file_view);
            },
            asio::detached
        );

        std::vector<std::thread> pool;
        pool.reserve(static_cast<std::size_t>(threads > 1 ? threads - 1 : 0));

        for (int i = 1; i < threads; ++i) {
            pool.emplace_back([&io_context]() {
                io_context.run();
            });
        }

        io_context.run();

        for (auto& t : pool) {
            t.join();
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}