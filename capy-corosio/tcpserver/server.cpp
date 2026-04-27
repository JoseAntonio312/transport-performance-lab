/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * TCP file server with Corosio
 * Minimal raw-byte server for performance and energy measurements.
 *
 */

#include <csignal>
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

#include <boost/capy/buffers.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/task.hpp>
#include <boost/corosio.hpp>

namespace fs = std::filesystem;
namespace corosio = boost::corosio;
namespace capy = boost::capy;

// Default TCP listening port.
constexpr int DEFAULT_PORT = 8080;

// Maximum number of pending connections in the listen queue.
constexpr int BACKLOG = 128;

// Maximum supported file size in memory.
// Increased to 2 GiB to allow large benchmark files.
constexpr std::uint64_t MAX_FILE_SIZE = 2ull * 1024ull * 1024ull * 1024ull;

// Heap-backed in-memory file storage.
// This avoids putting very large buffers on the stack.
struct FileBuffer {
    std::unique_ptr<char[]> data;
    std::size_t valid_size = 0;
};

// Load the whole file into memory before starting the server.
// This removes disk I/O from the measured serving phase.
static FileBuffer load_file_into_memory(const fs::path& path) {
    const std::uint64_t file_size_u64 = fs::file_size(path);

    if (file_size_u64 > MAX_FILE_SIZE) {
        throw std::runtime_error("File is larger than MAX_FILE_SIZE.");
    }

    const std::size_t file_size = static_cast<std::size_t>(file_size_u64);

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }

    FileBuffer file_buffer;
    file_buffer.valid_size = file_size;

    if (file_size > 0) {
        file_buffer.data = std::make_unique<char[]>(file_size);

        file.read(file_buffer.data.get(), static_cast<std::streamsize>(file_size));
        if (!file) {
            throw std::runtime_error("Failed to read file contents.");
        }
    }

    return file_buffer;
}

// Send the whole in-memory file to one client.
// The function keeps writing until all bytes are sent or the socket fails.
static capy::task<void> serve_client(
    corosio::tcp_socket sock,
    std::span<const char> file_view
) {
    std::size_t sent = 0;

    while (sent < file_view.size()) {
        auto [ec, n] = co_await sock.write_some(
            capy::const_buffer(file_view.data() + sent, file_view.size() - sent)
        );

        if (ec || n == 0) {
            break;
        }

        sent += static_cast<std::size_t>(n);
    }

    sock.close();
    co_return;
}

// Accept connections forever and spawn one coroutine per accepted client.
static capy::task<void> accept_loop(
    corosio::io_context& ctx,
    corosio::tcp_acceptor& acceptor,
    std::span<const char> file_view
) {
    while (true) {
        corosio::tcp_socket sock(ctx);
        auto [ec] = co_await acceptor.accept(sock);

        if (ec) {
            continue;
        }

        capy::run_async(ctx.get_executor())(
            serve_client(std::move(sock), file_view)
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

    const std::span<const char> file_view(
        file_buffer.valid_size > 0 ? file_buffer.data.get() : nullptr,
        file_buffer.valid_size
    );

    try {
        corosio::io_context ctx;
        corosio::tcp_acceptor acceptor(ctx);

        acceptor.open(corosio::tcp::v4());

        auto bind_ec = acceptor.bind(corosio::endpoint(static_cast<std::uint16_t>(port)));
        if (bind_ec) {
            std::cerr << "bind: " << bind_ec.message() << "\n";
            return 1;
        }

        auto listen_ec = acceptor.listen(BACKLOG);
        if (listen_ec) {
            std::cerr << "listen: " << listen_ec.message() << "\n";
            return 1;
        }

        capy::run_async(ctx.get_executor())(
            accept_loop(ctx, acceptor, file_view)
        );

        // Run the same io_context on N worker threads.
        // Each thread calls ctx.run(), so all of them participate in executing
        // the accept loop and the per-client send coroutines.
        std::vector<std::thread> pool;
        pool.reserve(static_cast<std::size_t>(threads));

        for (int i = 0; i < threads; ++i) {
            pool.emplace_back([&ctx]() {
                ctx.run();
            });
        }

        for (auto& worker : pool) {
            worker.join();
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}