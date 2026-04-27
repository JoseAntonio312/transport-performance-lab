/*
 * Copyright (c) 2026 Jose Antonio Garcia Montanez
 *
 * TCP file server with Asio
 * Minimal raw-byte server for performance and energy measurements.
 */

#include <csignal>

#include <asio.hpp>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using tcp = asio::ip::tcp;

constexpr int DEFAULT_PORT = 8080;
constexpr int BACKLOG = 128;
constexpr int MAX_THREADS = 256;

struct FileMapping {
    int fd = -1;
    const char* data = nullptr;
    std::size_t size = 0;
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
        close(mapping.fd);
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
        close(mapping.fd);
    }

    mapping.data = nullptr;
    mapping.fd = -1;
    mapping.size = 0;
}

static asio::awaitable<void> send_file(tcp::socket& socket, std::span<const char> payload) {
    std::size_t sent = 0;

    while (sent < payload.size()) {
        const std::size_t bytes_transferred = co_await socket.async_write_some(
            asio::buffer(payload.data() + sent, payload.size() - sent),
            asio::use_awaitable
        );

        if (bytes_transferred == 0) {
            break;
        }

        sent += bytes_transferred;
    }

    co_return;
}

static asio::awaitable<void> serve_client(tcp::socket socket, std::span<const char> payload) {
    try {
        co_await send_file(socket, payload);
    } catch (...) {
    }

    std::error_code close_ec;
    socket.shutdown(tcp::socket::shutdown_both, close_ec);
    socket.close(close_ec);

    co_return;
}

static asio::awaitable<void> accept_loop(tcp::acceptor& acceptor, std::span<const char> payload) {
    auto executor = co_await asio::this_coro::executor;

    while (true) {
        try {
            tcp::socket socket(executor);
            co_await acceptor.async_accept(socket, asio::use_awaitable);

            asio::co_spawn(
                executor,
                serve_client(std::move(socket), payload),
                asio::detached
            );
        } catch (...) {
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

    try {
        asio::io_context io_context;
        auto work_guard = asio::make_work_guard(io_context);

        tcp::acceptor acceptor(io_context);
        std::error_code ec;

        acceptor.open(tcp::v4(), ec);
        if (ec) {
            std::cerr << "open: " << ec.message() << "\n";
            unmap_file(mapping);
            return 1;
        }

        acceptor.set_option(asio::socket_base::reuse_address(true), ec);
        if (ec) {
            std::cerr << "set_option: " << ec.message() << "\n";
            unmap_file(mapping);
            return 1;
        }

        acceptor.bind(tcp::endpoint(tcp::v4(), static_cast<unsigned short>(port)), ec);
        if (ec) {
            std::cerr << "bind: " << ec.message() << "\n";
            unmap_file(mapping);
            return 1;
        }

        acceptor.listen(BACKLOG, ec);
        if (ec) {
            std::cerr << "listen: " << ec.message() << "\n";
            unmap_file(mapping);
            return 1;
        }

        asio::co_spawn(
            io_context,
            accept_loop(acceptor, payload),
            asio::detached
        );

        std::vector<std::thread> pool;
        pool.reserve(static_cast<std::size_t>(threads));

        for (int i = 0; i < threads; ++i) {
            pool.emplace_back([&io_context]() {
                io_context.run();
            });
        }

        for (auto& worker : pool) {
            worker.join();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        unmap_file(mapping);
        return 1;
    }

    unmap_file(mapping);
    return 0;
}