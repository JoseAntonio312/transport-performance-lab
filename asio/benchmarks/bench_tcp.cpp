/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * Asio file download benchmark
 * Minimal raw-byte benchmark for performance and energy measurements.
 *
 */

#include <benchmark/benchmark.h>

#include <asio.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>

using tcp = asio::ip::tcp;

// Default server TCP port.
constexpr int DEFAULT_PORT = 8080;

// Runtime-configurable server port.
static int g_port = DEFAULT_PORT;

// Fixed receive buffer size.
constexpr std::size_t BUFFER_SIZE = 8192;

// Download raw bytes until EOF.
static asio::awaitable<void> do_download(
    tcp::socket& socket,
    std::array<char, BUFFER_SIZE>& buffer
) {
    while (true) {
        const std::size_t bytes_transferred = co_await socket.async_read_some(
            asio::buffer(buffer.data(), buffer.size()),
            asio::use_awaitable
        );

        if (bytes_transferred == 0) {
            break;
        }
    }

    std::error_code close_ec;
    socket.shutdown(tcp::socket::shutdown_both, close_ec);
    socket.close(close_ec);

    co_return;
}

// Connect and run one full download session.
static asio::awaitable<void> do_session(
    const char* ip,
    int port,
    std::array<char, BUFFER_SIZE>& buffer
) {
    auto executor = co_await asio::this_coro::executor;
    tcp::socket socket(executor);

    const tcp::endpoint server(asio::ip::make_address(ip), static_cast<unsigned short>(port));

    co_await socket.async_connect(server, asio::use_awaitable);
    co_await do_download(socket, buffer);
}

// Benchmark one full file download.
static void BM_TCP_FileDownload(benchmark::State& state) {
    constexpr const char* ip = "127.0.0.1";
    const int port = g_port;

    for (auto _ : state) {
        asio::io_context io_context;
        std::array<char, BUFFER_SIZE> buffer{};

        std::exception_ptr eptr;

        asio::co_spawn(
            io_context,
            do_session(ip, port, buffer),
            [&eptr](std::exception_ptr e) {
                eptr = e;
            }
        );

        try {
            io_context.run();

            if (eptr) {
                std::rethrow_exception(eptr);
            }

            benchmark::DoNotOptimize(buffer.data());
            benchmark::ClobberMemory();

        } catch (const std::exception& e) {
            state.SkipWithError(e.what());
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