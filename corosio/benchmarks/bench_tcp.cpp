/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * Corosio file download benchmark
 * Minimal output version for performance and future energy measurements.
 */

#include <benchmark/benchmark.h>

#include <boost/corosio.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/buffers.hpp>

#include <arpa/inet.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// Puerto del servidor
constexpr int DEFAULT_PORT = 8080;

namespace corosio = boost::corosio;
namespace capy = boost::capy;

// Puerto global configurable
static int g_port = DEFAULT_PORT;

// Conexion TCP basica
static capy::task<bool> connect_tcp(corosio::tcp_socket& sock, const char* ip, int port) {
    sock.open();

    const corosio::endpoint server_ep(
        corosio::endpoint(ip),
        static_cast<unsigned short>(port));

    auto [ec] = co_await sock.connect(server_ep);
    co_return !ec;
}

// Recibir exactamente N bytes
static capy::task<bool> recv_exact(corosio::tcp_socket& sock, void* buffer, std::size_t total) {
    char* ptr = static_cast<char*>(buffer);
    std::size_t received = 0;

    while (received < total) {
        auto [ec, n] = co_await sock.read_some(
            capy::mutable_buffer(ptr + received, total - received));

        if (ec || n == 0) {
            co_return false;
        }

        received += static_cast<std::size_t>(n);
    }

    co_return true;
}

// Lectura uint32_t en red
static capy::task<std::uint32_t> read_u32(corosio::tcp_socket& sock) {
    std::uint32_t net = 0;

    if (!(co_await recv_exact(sock, &net, sizeof(net)))) {
        throw std::runtime_error("Error leyendo uint32.");
    }

    co_return ntohl(net);
}

// Lectura uint64_t en red
static capy::task<std::uint64_t> read_u64(corosio::tcp_socket& sock) {
    std::uint32_t high_net = 0;
    std::uint32_t low_net = 0;

    if (!(co_await recv_exact(sock, &high_net, sizeof(high_net))) ||
        !(co_await recv_exact(sock, &low_net, sizeof(low_net)))) {
        throw std::runtime_error("Error leyendo uint64.");
    }

    std::uint64_t high = ntohl(high_net);
    std::uint64_t low = ntohl(low_net);

    co_return (high << 32) | low;
}

// Descarga completa del fichero servido
static capy::task<bool> download_file(corosio::io_context& ioc,
                                      const char* ip,
                                      int port,
                                      std::vector<char>& buffer) {
    corosio::tcp_socket sock(ioc);

    if (!(co_await connect_tcp(sock, ip, port))) {
        co_return false;
    }

    try {
        std::uint32_t filename_size = co_await read_u32(sock);
        if (filename_size == 0 || filename_size > 4096) {
            sock.close();
            co_return false;
        }

        std::string filename(filename_size, '\0');
        if (!(co_await recv_exact(sock, filename.data(), filename_size))) {
            sock.close();
            co_return false;
        }

        std::uint64_t file_size = co_await read_u64(sock);
        std::uint64_t remaining = file_size;

        while (remaining > 0) {
            std::size_t chunk = static_cast<std::size_t>(
                remaining > buffer.size() ? buffer.size() : remaining
            );

            auto [ec, n] = co_await sock.read_some(
                capy::mutable_buffer(buffer.data(), chunk));

            if (ec || n == 0) {
                sock.close();
                co_return false;
            }

            remaining -= static_cast<std::uint64_t>(n);
        }

        benchmark::DoNotOptimize(buffer.data());
        benchmark::ClobberMemory();

        sock.close();
        co_return true;

    } catch (...) {
        sock.close();
        co_return false;
    }
}

// Benchmark de descarga del fichero servido
static void BM_TCP_FileDownload(benchmark::State& state) {
    const char* ip = "127.0.0.1";
    const int port = g_port;
    constexpr std::size_t BUFFER_SIZE = 8192;

    std::vector<char> buffer(BUFFER_SIZE);

    for (auto _ : state) {
        corosio::io_context ioc;
        bool ok = false;

        capy::run_async(ioc.get_executor())(
            [&]() -> capy::task<void> {
                ok = co_await download_file(ioc, ip, port, buffer);
            }()
        );

        ioc.run();

        if (!ok) {
            state.SkipWithError("Error en descarga.");
            break;
        }
    }
}

BENCHMARK(BM_TCP_FileDownload)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(1)
    ->UseRealTime();

int main(int argc, char** argv) {
    std::vector<char*> filtered_argv;
    filtered_argv.reserve(static_cast<std::size_t>(argc));
    filtered_argv.push_back(argv[0]);

    const std::string prefix = "--server_port=";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg.rfind(prefix, 0) == 0) {
            g_port = std::stoi(arg.substr(prefix.size()));
        } else {
            filtered_argv.push_back(argv[i]);
        }
    }

    int filtered_argc = static_cast<int>(filtered_argv.size());
    filtered_argv.push_back(nullptr);

    benchmark::Initialize(&filtered_argc, filtered_argv.data());
    if (benchmark::ReportUnrecognizedArguments(filtered_argc, filtered_argv.data())) {
        return 1;
    }

    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}