/*
 * Copyright (c) 2026 Jose Antonio Garcia Montanez
 *
 * TAPS raw-byte file download benchmark
 * Minimal benchmark version for performance and energy measurements.
 */

#include "taps/taps_api.h"

#include <benchmark/benchmark.h>

#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_awaitable.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

// Default server TCP port.
constexpr int DEFAULT_PORT = 8080;

// Runtime-configurable server port.
static int g_port = DEFAULT_PORT;

// Connect the TAPS connection to the benchmark server.
static asio::awaitable<std::unique_ptr<taps::Connection>> connect_to_server(
    taps::TransportServices& transport_services,
    const char* ip,
    int port
) {
    taps::TransportProperties properties;
    properties.set(taps::PropertyKey::RELIABILITY, taps::SelectionProperty::REQUIRE);
    properties.set(taps::PropertyKey::PRESERVE_ORDER, taps::SelectionProperty::REQUIRE);

    auto preconnection = transport_services.preconnect(
        taps::LocalEndpoint{},
        taps::RemoteEndpoint{ip, static_cast<std::uint16_t>(port)},
        std::move(properties)
    );

    auto connection_result = co_await preconnection.initiate();

    if (!connection_result) {
        co_return nullptr;
    }

    co_return std::move(*connection_result);
}

// Download raw bytes until the peer closes the connection.
// The benchmark does not write to disk; it only drains the socket.
static asio::awaitable<bool> download_file(
    asio::io_context& io_context,
    const char* ip,
    int port
) {
    taps::TransportServices transport_services(io_context);

    auto connection = co_await connect_to_server(transport_services, ip, port);
    if (!connection) {
        co_return false;
    }

    std::uint64_t total_bytes = 0;

    while (true) {
        auto receive_result = co_await connection->receive();

        if (!receive_result) {
            // In this raw-byte protocol, peer close after data is the normal EOF.
            break;
        }

        auto message = std::move(*receive_result);
        auto data = message.data();

        if (data.empty()) {
            break;
        }

        total_bytes += static_cast<std::uint64_t>(data.size());

        benchmark::DoNotOptimize(data.data());
        benchmark::DoNotOptimize(total_bytes);
        benchmark::ClobberMemory();
    }

    auto close_result = co_await connection->close();
    (void)close_result;

    co_return total_bytes > 0;
}

// Benchmark one full file download.
static void BM_TCP_FileDownload(benchmark::State& state) {
    constexpr const char* ip = "127.0.0.1";
    const int port = g_port;

    for (auto _ : state) {
        (void)_;

        asio::io_context io_context;
        bool ok = false;

        asio::co_spawn(
            io_context,
            [&]() -> asio::awaitable<void> {
                ok = co_await download_file(io_context, ip, port);
            },
            asio::detached
        );

        io_context.run();

        if (!ok) {
            state.SkipWithError("Download failed.");
            break;
        }
    }
}

BENCHMARK(BM_TCP_FileDownload)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(1)
    ->UseRealTime();

int main(int argc, char** argv) {
    const std::string prefix = "--server_port=";

    int filtered_argc = 1;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg.rfind(prefix, 0) == 0) {
            g_port = std::stoi(arg.substr(prefix.size()));
        } else {
            argv[filtered_argc++] = argv[i];
        }
    }

    argv[filtered_argc] = nullptr;

    benchmark::Initialize(&filtered_argc, argv);
    if (benchmark::ReportUnrecognizedArguments(filtered_argc, argv)) {
        return 1;
    }

    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}