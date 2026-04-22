/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * TAPS file download benchmark
 * Minimal raw-byte benchmark for performance and future energy measurements.
 */

#include <benchmark/benchmark.h>

#include "taps/taps_api.h"

#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_awaitable.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <utility>

// Default server port.
constexpr int DEFAULT_PORT = 8080;

// Configurable port passed from the benchmark command line.
static int g_port = DEFAULT_PORT;

// Small local scratch buffer used only so the benchmark has a stable object
// to hand to DoNotOptimize / ClobberMemory at the end of each run.
constexpr std::size_t SCRATCH_SIZE = 8192;

// Minimal benchmark session state.
struct BenchmarkState {
    std::unique_ptr<taps::Connection> connection;
    std::uint64_t total_bytes = 0;
    bool done = false;
    bool failed = false;

    explicit BenchmarkState(std::unique_ptr<taps::Connection> conn)
        : connection(std::move(conn)) {}
};

// Receive raw bytes until EOF.
// No payload is copied into a large application-managed buffer.
// The benchmark only counts the received bytes.
static asio::awaitable<void> do_download(
    std::shared_ptr<BenchmarkState> state
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

            state->total_bytes += static_cast<std::uint64_t>(data.size());
        }

        co_await state->connection->close();
        state->done = true;
        co_return;

    } catch (...) {
        state->failed = true;
        state->done = true;
        co_return;
    }
}

// Establish a TAPS connection and then download until EOF.
static asio::awaitable<void> do_session(
    asio::io_context& io_context,
    std::shared_ptr<BenchmarkState>& state,
    bool& setup_failed,
    const char* ip,
    int port
) {
    try {
        taps::TransportServices ts(io_context);

        taps::TransportProperties props;
        props.set(taps::PropertyKey::RELIABILITY, taps::SelectionProperty::REQUIRE);
        props.set(taps::PropertyKey::PRESERVE_ORDER, taps::SelectionProperty::REQUIRE);

        auto preconn = ts.preconnect(
            taps::LocalEndpoint{},
            taps::RemoteEndpoint{ip, static_cast<std::uint16_t>(port)},
            std::move(props)
        );

        auto init_result = co_await preconn.initiate();
        if (!init_result) {
            setup_failed = true;
            co_return;
        }

        state = std::make_shared<BenchmarkState>(std::move(*init_result));
        co_await do_download(state);

    } catch (...) {
        setup_failed = true;
        co_return;
    }
}

// Google Benchmark entry point.
// One benchmark iteration = one complete download from connect to EOF.
static void BM_TCP_FileDownload(benchmark::State& state) {
    constexpr const char* ip = "127.0.0.1";
    constexpr std::array<char, SCRATCH_SIZE> scratch{};

    for (auto _ : state) {
        (void)_;

        asio::io_context io_context;

        std::shared_ptr<BenchmarkState> session_state{};
        bool setup_failed = false;

        asio::co_spawn(
            io_context,
            do_session(io_context, session_state, setup_failed, ip, g_port),
            asio::detached
        );

        while ((!session_state && !setup_failed) || (session_state && !session_state->done)) {
            io_context.run_one();
        }

        if (setup_failed || !session_state || session_state->failed) {
            state.SkipWithError("Download failed.");
            break;
        }

        benchmark::DoNotOptimize(session_state->total_bytes);
        benchmark::DoNotOptimize(scratch.data());
        benchmark::ClobberMemory();
    }
}

BENCHMARK(BM_TCP_FileDownload)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(1)
    ->UseRealTime();

int main(int argc, char** argv) {
    std::string prefix = "--server_port=";

    int filtered_argc = 0;
    std::array<char*, 128> filtered_argv{};

    filtered_argv[filtered_argc++] = argv[0];

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg.rfind(prefix, 0) == 0) {
            g_port = std::stoi(arg.substr(prefix.size()));
        } else {
            filtered_argv[filtered_argc++] = argv[i];
        }
    }

    filtered_argv[filtered_argc] = nullptr;

    benchmark::Initialize(&filtered_argc, filtered_argv.data());
    if (benchmark::ReportUnrecognizedArguments(filtered_argc, filtered_argv.data())) {
        return 1;
    }

    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}