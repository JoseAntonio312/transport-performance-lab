/*
 * Copyright (c) 2026 Jose Antonio Garcia Montanez
 *
 * TCP file client with TAPS
 * Minimal raw-byte client for performance and energy measurements.
 */

#include "taps/taps_api.h"

#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_awaitable.hpp>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

// Default remote TCP port.
constexpr int DEFAULT_PORT = 8080;

// Connect the TAPS connection to the benchmark server.
static asio::awaitable<std::unique_ptr<taps::Connection>> connect_to_server(
    taps::TransportServices& transport_services,
    const std::string& server_ip,
    int port
) {
    taps::TransportProperties properties;
    properties.set(taps::PropertyKey::RELIABILITY, taps::SelectionProperty::REQUIRE);
    properties.set(taps::PropertyKey::PRESERVE_ORDER, taps::SelectionProperty::REQUIRE);

    auto preconnection = transport_services.preconnect(
        taps::LocalEndpoint{},
        taps::RemoteEndpoint{server_ip, static_cast<std::uint16_t>(port)},
        std::move(properties)
    );

    auto connection_result = co_await preconnection.initiate();

    if (!connection_result) {
        std::cerr << "connect failed: " << connection_result.error().message() << "\n";
        co_return nullptr;
    }

    co_return std::move(*connection_result);
}

// Receive raw bytes until EOF/close and write them directly to disk.
static asio::awaitable<bool> receive_file(
    taps::Connection& connection,
    const std::string& output_path
) {
    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        std::cerr << "Failed to open output file: " << output_path << "\n";
        co_return false;
    }

    std::uint64_t total_bytes = 0;

    while (true) {
        auto receive_result = co_await connection.receive();

        if (!receive_result) {
            // In this raw-byte protocol, peer close after data is the normal EOF.
            break;
        }

        auto message = std::move(*receive_result);
        auto data = message.data();

        if (data.empty()) {
            break;
        }

        out.write(
            reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size())
        );

        if (!out) {
            std::cerr << "Failed to write output file.\n";
            co_await connection.close();
            co_return false;
        }

        total_bytes += static_cast<std::uint64_t>(data.size());
    }

    out.close();

    auto close_result = co_await connection.close();
    (void)close_result;

    co_return total_bytes > 0;
}

static asio::awaitable<bool> run_client(
    asio::io_context& io_context,
    const std::string& server_ip,
    int port,
    const std::string& output_path
) {
    taps::TransportServices transport_services(io_context);

    auto connection = co_await connect_to_server(transport_services, server_ip, port);
    if (!connection) {
        co_return false;
    }

    co_return co_await receive_file(*connection, output_path);
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
        bool ok = false;

        asio::co_spawn(
            io_context,
            [&]() -> asio::awaitable<void> {
                ok = co_await run_client(io_context, server_ip, port, output_path);
            },
            asio::detached
        );

        io_context.run();
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}