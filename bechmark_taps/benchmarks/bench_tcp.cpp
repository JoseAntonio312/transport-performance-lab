/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * TAPS file download benchmark
 * Minimal output version for performance and future energy measurements.
 */

#include <benchmark/benchmark.h>

#include "taps/taps_api.h"
#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_awaitable.hpp>

#include <arpa/inet.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Alias asio
namespace asio = ::asio;

// Puerto del servidor
constexpr int DEFAULT_PORT = 8080;

// Buffer de recepcion
constexpr std::size_t BUFFER_SIZE = 8192;

// Estructura estado cliente
struct ClientState {
    std::unique_ptr<taps::Connection> connection; // connection descriptor
    std::vector<char> buffer;                     // reusable buffer
    std::string filename;                         // filename
    std::uint64_t file_size = 0;                 // file size
    std::uint64_t remaining = 0;                 // remaining bytes
    std::uint32_t filename_size = 0;             // filename size
    std::uint32_t high_net = 0;                  // high uint32
    std::uint32_t low_net = 0;                   // low uint32
    std::vector<char> stream_buffer;             // stream buffer
    std::size_t stream_offset = 0;               // consumed bytes
    bool done = false;                           // end state
    bool failed = false;                         // error state

    explicit ClientState(std::unique_ptr<taps::Connection> conn, std::size_t buffer_size)
        : connection(std::move(conn)), buffer(buffer_size) {
        stream_buffer.reserve(buffer_size * 2);
    }
};

// Recibir exactamente N bytes
static asio::awaitable<bool> recv_exact(const std::shared_ptr<ClientState>& client,
                                        void* buffer,
                                        std::size_t total) {
    char* ptr = static_cast<char*>(buffer);
    std::size_t received = 0;

    while (received < total) {
        const std::size_t available = client->stream_buffer.size() - client->stream_offset;
        if (available > 0) {
            const std::size_t chunk = std::min(total - received, available);
            std::memcpy(ptr + received,
                        client->stream_buffer.data() + client->stream_offset,
                        chunk);

            client->stream_offset += chunk;
            received += chunk;

            if (client->stream_offset == client->stream_buffer.size()) {
                client->stream_buffer.clear();
                client->stream_offset = 0;
            }

            continue;
        }

        auto receive_result = co_await client->connection->receive();
        if (!receive_result) {
            co_return false;
        }

        auto& data = receive_result->data();
        if (data.empty()) {
            co_return false;
        }

        client->stream_buffer.resize(data.size());
        std::memcpy(client->stream_buffer.data(), data.data(), data.size());
        client->stream_offset = 0;
    }

    co_return true;
}

// Lectura uint32_t en red
static asio::awaitable<std::uint32_t> read_u32(const std::shared_ptr<ClientState>& client) {
    std::uint32_t net = 0;

    if (!(co_await recv_exact(client, &net, sizeof(net)))) {
        throw std::runtime_error("Error leyendo uint32.");
    }

    co_return ntohl(net);
}

// Lectura uint64_t en red
static asio::awaitable<std::uint64_t> read_u64(const std::shared_ptr<ClientState>& client) {
    std::uint32_t high_net = 0;
    std::uint32_t low_net = 0;

    if (!(co_await recv_exact(client, &high_net, sizeof(high_net))) ||
        !(co_await recv_exact(client, &low_net, sizeof(low_net)))) {
        throw std::runtime_error("Error leyendo uint64.");
    }

    std::uint64_t high = ntohl(high_net);
    std::uint64_t low = ntohl(low_net);

    co_return (high << 32) | low;
}

// Benchmark de descarga del fichero servido
// Protocolo:
// [u32 tam_nombre][nombre][u64 tam_fichero][contenido]
static void BM_TCP_FileDownload(benchmark::State& state) {
    const char* ip = "127.0.0.1";
    const int port = DEFAULT_PORT;

    for (auto _ : state) {
        asio::io_context io_context;

        auto client = std::shared_ptr<ClientState>{};
        bool setup_failed = false;

        asio::co_spawn(
            io_context,
            [&io_context, &client, &setup_failed, ip, port]() -> asio::awaitable<void> {
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

                    client = std::make_shared<ClientState>(std::move(*init_result), BUFFER_SIZE);

                    // Read filename size
                    client->filename_size = co_await read_u32(client);
                    if (client->filename_size == 0 || client->filename_size > 4096) {
                        client->failed = true;
                        client->done = true;
                        co_return;
                    }

                    // Read filename
                    client->filename.resize(client->filename_size);
                    if (!(co_await recv_exact(client, client->filename.data(), client->filename_size))) {
                        client->failed = true;
                        client->done = true;
                        co_return;
                    }

                    // Read file size
                    client->file_size = co_await read_u64(client);
                    client->remaining = client->file_size;

                    // Read content
                    while (client->remaining > 0) {
                        const std::size_t available = client->stream_buffer.size() - client->stream_offset;
                        if (available > 0) {
                            const std::size_t chunk = static_cast<std::size_t>(
                                std::min<std::uint64_t>(client->remaining, available)
                            );

                            client->remaining -= static_cast<std::uint64_t>(chunk);
                            client->stream_offset += chunk;

                            if (client->stream_offset == client->stream_buffer.size()) {
                                client->stream_buffer.clear();
                                client->stream_offset = 0;
                            }

                            continue;
                        }

                        auto receive_result = co_await client->connection->receive();
                        if (!receive_result) {
                            client->failed = true;
                            client->done = true;
                            co_return;
                        }

                        auto& data = receive_result->data();
                        if (data.empty()) {
                            client->failed = true;
                            client->done = true;
                            co_return;
                        }

                        const std::size_t chunk = static_cast<std::size_t>(
                            std::min<std::uint64_t>(client->remaining, data.size())
                        );

                        client->remaining -= static_cast<std::uint64_t>(chunk);

                        // Guardar sobrante, si existe
                        if (chunk < data.size()) {
                            const std::size_t leftover = data.size() - chunk;
                            client->stream_buffer.resize(leftover);
                            std::memcpy(client->stream_buffer.data(), data.data() + chunk, leftover);
                            client->stream_offset = 0;
                        }
                    }

                    co_await client->connection->close();
                    client->done = true;

                } catch (...) {
                    if (client) {
                        client->failed = true;
                        client->done = true;
                    } else {
                        setup_failed = true;
                    }
                }
            },
            asio::detached
        );

        // Loop
        while ((!client && !setup_failed) || (client && !client->done)) {
            io_context.run_one();
        }

        if (setup_failed || !client || client->failed) {
            state.SkipWithError("Error en descarga.");
            break;
        }

        benchmark::DoNotOptimize(client->buffer.data());
        benchmark::ClobberMemory();
    }
}

BENCHMARK(BM_TCP_FileDownload)
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();