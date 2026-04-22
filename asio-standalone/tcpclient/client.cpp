/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * TCP file client with Asio
 * Minimal raw-byte client for performance and energy measurements.
 */

#include <asio.hpp>

#include <array>
#include <cstddef>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>

using tcp = asio::ip::tcp;

// Default remote TCP port.
constexpr int DEFAULT_PORT = 8080;

// Fixed receive buffer size.
constexpr std::size_t BUFFER_SIZE = 8192;

// Receive raw bytes until EOF and write them directly to disk.
static asio::awaitable<void> do_download(
    tcp::socket& socket,
    const std::string& output_path
) {
    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Failed to open output file: " + output_path);
    }

    std::array<char, BUFFER_SIZE> buffer{};

    while (true) {
        const std::size_t bytes_transferred = co_await socket.async_read_some(
            asio::buffer(buffer.data(), buffer.size()),
            asio::use_awaitable
        );

        if (bytes_transferred == 0) {
            break;
        }

        out.write(buffer.data(), static_cast<std::streamsize>(bytes_transferred));
        if (!out) {
            throw std::runtime_error("Failed to write output file.");
        }
    }

    out.close();

    std::error_code close_ec;
    socket.shutdown(tcp::socket::shutdown_both, close_ec);
    socket.close(close_ec);

    co_return;
}

// Connect and run one full download session.
static asio::awaitable<void> do_session(
    const std::string& server_ip,
    int port,
    const std::string& output_path
) {
    auto executor = co_await asio::this_coro::executor;
    tcp::socket socket(executor);

    const tcp::endpoint server(asio::ip::make_address(server_ip), static_cast<unsigned short>(port));

    co_await socket.async_connect(server, asio::use_awaitable);
    co_await do_download(socket, output_path);
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

    try {
        asio::io_context io_context;
        std::exception_ptr eptr;

        asio::co_spawn(
            io_context,
            do_session(server_ip, port, output_path),
            [&eptr](std::exception_ptr e) {
                eptr = e;
            }
        );

        io_context.run();

        if (eptr) {
            std::rethrow_exception(eptr);
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}