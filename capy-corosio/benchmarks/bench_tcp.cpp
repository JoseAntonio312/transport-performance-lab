/*
 * Copyright (c) 2026 Jose Antonio Garcia Montanez
 *
 * Corosio raw-byte file download benchmark
 * Minimal benchmark version for performance and energy measurements.
 */

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>

#include <boost/capy/buffers.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/task.hpp>
#include <boost/corosio.hpp>

namespace corosio = boost::corosio;
namespace capy = boost::capy;

constexpr int DEFAULT_PORT = 8080;
constexpr std::size_t BUFFER_SIZE = 8192;

static int g_port = DEFAULT_PORT;

static capy::task<bool> connect_to_server(corosio::tcp_socket& socket,
                                          const char* ip,
                                          int port) {
    socket.open();

    auto [ec] = co_await socket.connect(
        corosio::endpoint(corosio::endpoint(ip), static_cast<unsigned short>(port))
    );

    co_return !ec;
}

static bool is_clean_eof(const std::error_code& ec) {
    if (!ec) {
        return false;
    }

    if (ec == std::errc::connection_reset) {
        return true;
    }

    const std::string message = ec.message();
    return message == "End of file" ||
           message == "end of file" ||
           message == "EOF" ||
           message == "eof";
}

static capy::task<bool> receive_file(corosio::tcp_socket& socket,
                                     std::array<char, BUFFER_SIZE>& buffer,
                                     std::uint64_t& total_bytes) {
    total_bytes = 0;

    while (true) {
        auto [ec, n] = co_await socket.read_some(
            capy::mutable_buffer(buffer.data(), buffer.size())
        );

        if (n > 0) {
            total_bytes += static_cast<std::uint64_t>(n);
            benchmark::DoNotOptimize(buffer.data());
            benchmark::DoNotOptimize(total_bytes);
            benchmark::ClobberMemory();
            continue;
        }

        if (!ec && n == 0) {
            break;
        }

        if (ec) {
            if (total_bytes > 0 && is_clean_eof(ec)) {
                break;
            }

            co_return false;
        }
    }

    co_return total_bytes > 0;
}

static capy::task<bool> run_benchmark_client(corosio::io_context& context,
                                             const char* ip,
                                             int port,
                                             std::array<char, BUFFER_SIZE>& buffer,
                                             std::uint64_t& total_bytes) {
    corosio::tcp_socket socket(context);

    if (!(co_await connect_to_server(socket, ip, port))) {
        co_return false;
    }

    const bool ok = co_await receive_file(socket, buffer, total_bytes);
    socket.close();

    co_return ok;
}

static void BM_TCP_FileDownload(benchmark::State& state) {
    constexpr const char* ip = "127.0.0.1";
    const int port = g_port;

    std::uint64_t bytes_processed = 0;
    std::uint64_t last_downloaded_bytes = 0;

    for (auto _ : state) {
        (void)_;

        corosio::io_context context;
        std::array<char, BUFFER_SIZE> buffer{};
        std::uint64_t downloaded_bytes = 0;
        bool ok = false;

        capy::run_async(context.get_executor())(
            [&]() -> capy::task<void> {
                ok = co_await run_benchmark_client(context, ip, port, buffer, downloaded_bytes);
            }()
        );

        context.run();

        if (!ok) {
            state.SkipWithError("Download failed.");
            break;
        }

        bytes_processed += downloaded_bytes;
        last_downloaded_bytes = downloaded_bytes;
    }

    state.SetBytesProcessed(static_cast<int64_t>(bytes_processed));
    state.counters["downloaded_bytes"] = static_cast<double>(last_downloaded_bytes);
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