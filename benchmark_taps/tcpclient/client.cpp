/*
 * Copyright (c) 2026 José Antonio García Montañez
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

#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

// Default remote TCP port.
constexpr int DEFAULT_PORT = 8080;

// Minimal client session state.
struct ClientState {
    std::unique_ptr<taps::Connection> connection;
    std::ofstream out;
    bool done = false;
    bool failed = false;

    explicit ClientState(std::unique_ptr<taps::Connection> conn)
        : connection(std::move(conn)) {}
};

// Receive raw bytes until EOF and write them directly to disk.
static asio::awaitable<void> do_download(
    std::shared_ptr<ClientState> state
) {
    try {
        while (true) {
            auto receive_result = co_await state->connection->receive();

            if (!receive_result) {
                break;
            }

            auto& data = receive_result->data();
            if (data.empty()) {
                break;
            }

            state->out.write(
                reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size())
            );

            if (!state->out) {
                state->failed = true;
                state->done = true;
                co_return;
            }
        }

        state->out.close();
        co_await state->connection->close();
        state->done = true;
        co_return;

    } catch (...) {
        state->failed = true;
        state->done = true;
        co_return;
    }
}

// Establish the connection, open the output file, and run the download.
static asio::awaitable<void> do_session(
    asio::io_context& io_context,
    std::shared_ptr<ClientState>& state,
    std::exception_ptr& eptr,
    std::string server_ip,
    int port,
    std::string output_path
) {
    try {
        taps::TransportServices ts(io_context);

        taps::TransportProperties props;
        props.set(taps::PropertyKey::RELIABILITY, taps::SelectionProperty::REQUIRE);
        props.set(taps::PropertyKey::PRESERVE_ORDER, taps::SelectionProperty::REQUIRE);

        auto preconn = ts.preconnect(
            taps::LocalEndpoint{},
            taps::RemoteEndpoint{server_ip, static_cast<std::uint16_t>(port)},
            std::move(props)
        );

        auto init_result = co_await preconn.initiate();
        if (!init_result) {
            throw std::runtime_error("connect failed.");
        }

        state = std::make_shared<ClientState>(std::move(*init_result));

        state->out.open(output_path, std::ios::binary);
        if (!state->out) {
            throw std::runtime_error("Failed to open output file: " + output_path);
        }

        co_await do_download(state);

        if (state->failed) {
            throw std::runtime_error("Download failed.");
        }

    } catch (...) {
        eptr = std::current_exception();
        co_return;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: " << argv[0] << " <server_ip> [port] [output_file]\n";
        return 1;
    }

    std::string server_ip = argv[1];
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
        std::shared_ptr<ClientState> state{};
        std::exception_ptr eptr{};

        asio::co_spawn(
            io_context,
            do_session(
                io_context,
                state,
                eptr,
                std::move(server_ip),
                port,
                std::move(output_path)
            ),
            asio::detached
        );

        while ((!state && !eptr) || (state && !state->done && !eptr)) {
            io_context.run_one();
        }

        if (eptr) {
            std::rethrow_exception(eptr);
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}