/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * Asio file download benchmark
 * Minimal output version for performance and future energy measurements.
 */

#include <benchmark/benchmark.h>

#include <asio.hpp>

#include <arpa/inet.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using tcp = asio::ip::tcp;

// Puerto del servidor
constexpr int DEFAULT_PORT = 8080;

// Puerto global configurable
static int g_port = DEFAULT_PORT;

// Estructura estado cliente
struct ClientState {
    tcp::socket socket;
    std::vector<char> buffer;
    std::string filename;
    std::uint64_t file_size = 0;
    std::uint64_t remaining = 0;
    std::uint32_t filename_size = 0;
    std::uint32_t high_net = 0;
    std::uint32_t low_net = 0;

    explicit ClientState(asio::io_context& io_context, std::size_t buffer_size)
        : socket(io_context), buffer(buffer_size) {}
};

// Lectura uint32_t en red
static std::uint32_t read_u32(std::uint32_t net) {
    return ntohl(net);
}

// Lectura uint64_t en red
static std::uint64_t read_u64(std::uint32_t high_net, std::uint32_t low_net) {
    std::uint64_t high = ntohl(high_net);
    std::uint64_t low = ntohl(low_net);

    return (high << 32) | low;
}

// Async content read
static asio::awaitable<void> do_read_content(const std::shared_ptr<ClientState>& client) {
    while (client->remaining > 0) {
        std::size_t chunk = static_cast<std::size_t>(
            client->remaining > client->buffer.size() ? client->buffer.size() : client->remaining
        );

        std::size_t bytes_transferred = co_await client->socket.async_read_some(
            asio::buffer(client->buffer.data(), chunk),
            asio::use_awaitable
        );

        if (bytes_transferred == 0) {
            throw std::runtime_error("Conexion cerrada antes de tiempo.");
        }

        client->remaining -= static_cast<std::uint64_t>(bytes_transferred);
    }

    std::error_code close_ec;
    client->socket.shutdown(tcp::socket::shutdown_both, close_ec);
    client->socket.close(close_ec);

    co_return;
}

// Async file size read low
static asio::awaitable<void> do_read_file_size_low(const std::shared_ptr<ClientState>& client) {
    std::size_t bytes_transferred = co_await asio::async_read(
        client->socket,
        asio::buffer(&client->low_net, sizeof(client->low_net)),
        asio::use_awaitable
    );

    if (bytes_transferred != sizeof(client->low_net)) {
        throw std::runtime_error("Error leyendo uint64.");
    }

    client->file_size = read_u64(client->high_net, client->low_net);
    client->remaining = client->file_size;

    co_await do_read_content(client);
}

// Async file size read high
static asio::awaitable<void> do_read_file_size_high(const std::shared_ptr<ClientState>& client) {
    std::size_t bytes_transferred = co_await asio::async_read(
        client->socket,
        asio::buffer(&client->high_net, sizeof(client->high_net)),
        asio::use_awaitable
    );

    if (bytes_transferred != sizeof(client->high_net)) {
        throw std::runtime_error("Error leyendo uint64.");
    }

    co_await do_read_file_size_low(client);
}

// Async filename read
static asio::awaitable<void> do_read_filename(const std::shared_ptr<ClientState>& client) {
    client->filename.resize(client->filename_size);

    std::size_t bytes_transferred = co_await asio::async_read(
        client->socket,
        asio::buffer(client->filename.data(), client->filename_size),
        asio::use_awaitable
    );

    if (bytes_transferred != client->filename_size) {
        throw std::runtime_error("Error leyendo nombre.");
    }

    co_await do_read_file_size_high(client);
}

// Async filename size read
static asio::awaitable<void> do_read_filename_size(const std::shared_ptr<ClientState>& client) {
    std::size_t bytes_transferred = co_await asio::async_read(
        client->socket,
        asio::buffer(&client->filename_size, sizeof(client->filename_size)),
        asio::use_awaitable
    );

    if (bytes_transferred != sizeof(client->filename_size)) {
        throw std::runtime_error("Error leyendo uint32.");
    }

    client->filename_size = read_u32(client->filename_size);
    if (client->filename_size == 0 || client->filename_size > 4096) {
        throw std::runtime_error("Tamano de nombre invalido.");
    }

    co_await do_read_filename(client);
}

// Async session
static asio::awaitable<void> do_session(const std::shared_ptr<ClientState>& client,
                                        const char* ip,
                                        int port) {
    tcp::endpoint server(asio::ip::make_address(ip), static_cast<unsigned short>(port));

    co_await client->socket.async_connect(server, asio::use_awaitable);
    co_await do_read_filename_size(client);
}

// Benchmark de descarga del fichero servido
static void BM_TCP_FileDownload(benchmark::State& state) {
    const char* ip = "127.0.0.1";
    const int port = g_port;
    constexpr std::size_t BUFFER_SIZE = 8192;

    for (auto _ : state) {
        asio::io_context io_context;
        auto client = std::make_shared<ClientState>(io_context, BUFFER_SIZE);

        std::exception_ptr eptr;

        asio::co_spawn(
            io_context,
            do_session(client, ip, port),
            [&eptr](std::exception_ptr e) {
                eptr = e;
            }
        );

        try {
            io_context.run();

            if (eptr) {
                std::rethrow_exception(eptr);
            }

            benchmark::DoNotOptimize(client->buffer.data());
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