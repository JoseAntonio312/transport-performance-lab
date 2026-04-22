/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * Corosio raw-byte file download benchmark
 * Minimal benchmark version for performance and energy measurements.
 *
 */

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <boost/capy/buffers.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/task.hpp>
#include <boost/corosio.hpp>

namespace corosio = boost::corosio;
namespace capy = boost::capy;

// Default server port.
constexpr int DEFAULT_PORT = 8080;

// Fixed receive buffer size used by the benchmark client.
constexpr std::size_t BUFFER_SIZE = 8192;

// Global configurable port, optionally overridden with --server_port=...
static int g_port = DEFAULT_PORT;

// Connect the socket to the benchmark server.
// Returns true on success, false on failure.
static capy::task<bool> connect_tcp(corosio::tcp_socket& sock, const char* ip, int port) {
    sock.open();

    auto [ec] = co_await sock.connect(
        corosio::endpoint(corosio::endpoint(ip), static_cast<unsigned short>(port))
    );

    co_return !ec;
}

// Download raw bytes until the peer closes the connection.
// The function does not store the file permanently; it only drains the socket
// into a fixed-size stack buffer so the benchmark measures network + runtime cost
// without adding file-system write overhead.
//
// Returns true on success, false on failure.
static capy::task<bool> download_stream(
    corosio::io_context& ctx,
    const char* ip,
    int port,
    std::array<char, BUFFER_SIZE>& buffer
) {
    corosio::tcp_socket sock(ctx);

    if (!(co_await connect_tcp(sock, ip, port))) {
        co_return false;
    }

    std::uint64_t total_bytes = 0;

    while (true) {
        auto [ec, n] = co_await sock.read_some(
            capy::mutable_buffer(buffer.data(), buffer.size())
        );

        if (ec) {
            sock.close();
            co_return false;
        }

        // EOF: the server finished sending the file and closed the connection.
        if (n == 0) {
            break;
        }

        total_bytes += static_cast<std::uint64_t>(n);

        // Prevent the compiler from treating the received data path as dead.
        benchmark::DoNotOptimize(buffer.data());
        benchmark::DoNotOptimize(total_bytes);
        benchmark::ClobberMemory();
    }

    sock.close();
    co_return true;
}

// Google Benchmark entry point.
// Each iteration performs one full file download from the server.
static void BM_TCP_FileDownload(benchmark::State& state) {
    const char* ip = "127.0.0.1";
    const int port = g_port;

    for (auto _ : state) {
        (void)_;

        corosio::io_context ctx;
        std::array<char, BUFFER_SIZE> buffer{};
        bool ok = false;

        /*
         * run_async needs a top-level coroutine to schedule on the executor.
         *
         * download_stream(...) returns capy::task<bool>, so this small wrapper
         * coroutine exists only to co_await that task and store its boolean
         * result into the local variable 'ok'.
         */
        capy::run_async(ctx.get_executor())(
            [&]() -> capy::task<void> {
                ok = co_await download_stream(ctx, ip, port, buffer);
            }()
        );

        // Run the event loop until the scheduled asynchronous work completes.
        ctx.run();

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

    // Filter our custom argument in-place so Google Benchmark only sees
    // the arguments that belong to it.
    int write_idx = 1;

    for (int read_idx = 1; read_idx < argc; ++read_idx) {
        const std::string arg = argv[read_idx];

        if (arg.rfind(prefix, 0) == 0) {
            g_port = std::stoi(arg.substr(prefix.size()));
        } else {
            argv[write_idx++] = argv[read_idx];
        }
    }

    argc = write_idx;
    argv[argc] = nullptr;

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }

    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}