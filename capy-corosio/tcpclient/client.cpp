/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * TCP file client with Corosio
 * Minimal raw-byte client for performance and energy measurements.
 *
 */

#include <array>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <string>

#include <boost/capy/buffers.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/task.hpp>
#include <boost/corosio.hpp>

namespace corosio = boost::corosio;
namespace capy = boost::capy;

// Default remote TCP port.
constexpr int DEFAULT_PORT = 8080;

// Fixed receive buffer size used for socket reads.
constexpr std::size_t BUFFER_SIZE = 8192;

// Connect to the server and receive raw bytes until EOF.
// All received data is written directly to the output file.
//
// Return value:
// - 0 on success
// - 1 on failure
static capy::task<int> run_client(
    corosio::io_context& ctx,
    const std::string& server_ip,
    int port,
    const std::string& output_path
) {
    // Create the TCP socket bound to the Corosio I/O context.
    corosio::tcp_socket sock(ctx);

    // Open the socket before connecting.
    sock.open();

    // Start the asynchronous TCP connection.
    auto [connect_ec] = co_await sock.connect(
        corosio::endpoint(corosio::endpoint(server_ip), static_cast<unsigned short>(port))
    );

    // Abort if the connection fails.
    if (connect_ec) {
        std::cerr << "connect: " << connect_ec.message() << "\n";
        co_return 1;
    }

    // Open the destination file in binary mode.
    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        std::cerr << "Failed to open output file: " << output_path << "\n";
        sock.close();
        co_return 1;
    }

    // Fixed stack-owned receive buffer.
    // This avoids dynamic allocation during the transfer loop.
    std::array<char, BUFFER_SIZE> buffer{};

    while (true) {
        // Read up to BUFFER_SIZE bytes from the socket.
        auto [read_ec, n] = co_await sock.read_some(
            capy::mutable_buffer(buffer.data(), buffer.size())
        );

        // Any socket read error is treated as a failure.
        if (read_ec) {
            std::cerr << "recv: " << read_ec.message() << "\n";
            sock.close();
            co_return 1;
        }

        // A zero-length read means EOF: the server closed the connection
        // after sending the whole file.
        if (n == 0) {
            break;
        }

        // Write the received bytes directly to disk.
        out.write(buffer.data(), static_cast<std::streamsize>(n));
        if (!out) {
            std::cerr << "Failed to write output file.\n";
            sock.close();
            co_return 1;
        }
    }

    // Clean shutdown path.
    out.close();
    sock.close();
    co_return 0;
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
        corosio::io_context ctx;
        int result = 1;

        // run_client(...) returns capy::task<int>, but it is still just a coroutine object.
        // It does not run by itself until something schedules/starts it.
        //
        // capy::run_async(...) is used here to launch asynchronous work on the executor.
        // We wrap run_client(...) inside another coroutine:
        //
        //   [&]() -> capy::task<void> { result = co_await run_client(...); }()
        //
        // Why this extra wrapper exists:
        // 1. run_client returns int, but here we want to store that result into the local
        //    variable 'result' owned by main().
        // 2. The outer coroutine becomes the scheduled top-level task.
        // 3. Inside that top-level task, we co_await the real client coroutine and copy
        //    its final integer status into 'result'.
        //
        // So the structure is:
        // - outer task<void>: launchable root coroutine
        // - inner task<int>: actual client logic
        //
        // This is why it may look like a "double task", even though only one real client
        // operation is being performed.
        capy::run_async(ctx.get_executor())(
            [&]() -> capy::task<void> {
                result = co_await run_client(ctx, server_ip, port, output_path);
            }()
        );

        // Drive the event loop until all scheduled asynchronous work completes.
        ctx.run();

        return result;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}