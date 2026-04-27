/*
 * Copyright (c) 2026 Jose Antonio Garcia Montanez
 *
 * TCP file client with Corosio
 * Minimal raw-byte client for performance and energy measurements.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

#include <boost/capy/buffers.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/task.hpp>
#include <boost/corosio.hpp>

namespace corosio = boost::corosio;
namespace capy = boost::capy;

constexpr int DEFAULT_PORT = 8080;
constexpr std::size_t BUFFER_SIZE = 8192;

static capy::task<bool> connect_to_server(
    corosio::tcp_socket& socket,
    const std::string& server_ip,
    int port
) {
    socket.open();

    auto [ec] = co_await socket.connect(
        corosio::endpoint(
            corosio::endpoint(server_ip.c_str()),
            static_cast<unsigned short>(port)
        )
    );

    co_return !ec;
}

static capy::task<bool> receive_file(
    corosio::tcp_socket& socket,
    const std::string& output_path
) {
    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        std::cerr << "Failed to open output file: " << output_path << "\n";
        co_return false;
    }

    std::array<char, BUFFER_SIZE> buffer{};
    std::uint64_t total_bytes = 0;

    while (true) {
        auto [ec, n] = co_await socket.read_some(
            capy::mutable_buffer(buffer.data(), buffer.size())
        );

        if (n > 0) {
            total_bytes += static_cast<std::uint64_t>(n);
            out.write(buffer.data(), static_cast<std::streamsize>(n));
            if (!out) {
                std::cerr << "Failed to write output file.\n";
                co_return false;
            }
            continue;
        }

        // In this raw-byte protocol EOF is the normal transfer terminator.
        if (n == 0) {
            break;
        }

        if (ec) {
            socket.close();
            co_return false;
        }
    }

    socket.close();
    out.close();
    co_return total_bytes > 0;
}

static capy::task<bool> run_client(
    corosio::io_context& context,
    const std::string& server_ip,
    int port,
    const std::string& output_path
) {
    corosio::tcp_socket socket(context);

    if (!(co_await connect_to_server(socket, server_ip, port))) {
        std::cerr << "connect failed\n";
        co_return false;
    }

    co_return co_await receive_file(socket, output_path);
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

    corosio::io_context context;
    bool ok = false;

    capy::run_async(context.get_executor())(
        [&]() -> capy::task<void> {
            ok = co_await run_client(context, server_ip, port, output_path);
        }()
    );

    context.run();
    return ok ? 0 : 1;
}