/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * TCP file client with TAPS
 * Minimal output version for performance and energy measurements.
 */

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
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Puerto por defecto
constexpr int DEFAULT_PORT = 8080;

// Buffer de recepcion
constexpr std::size_t BUFFER_SIZE = 8192;

// Estructura estado cliente
struct ClientState {
    std::unique_ptr<taps::Connection> connection; // connection descriptor
    std::vector<char> buffer;                     // reusable buffer
    std::ofstream out;                            // output file
    std::string filename;                         // filename
    std::string custom_output_name;               // custom output
    std::uint64_t file_size = 0;                  // file size
    std::uint64_t remaining = 0;                  // remaining bytes
    std::uint32_t filename_size = 0;              // filename size
    std::uint32_t high_net = 0;                   // high uint32
    std::uint32_t low_net = 0;                    // low uint32
    std::vector<char> stream_buffer;              // stream buffer
    std::size_t stream_offset = 0;                // consumed bytes

    explicit ClientState(std::unique_ptr<taps::Connection> conn)
        : connection(std::move(conn)), buffer(BUFFER_SIZE) {
        stream_buffer.reserve(BUFFER_SIZE * 2);
    }
};

// Recibir exactamente N bytes
static asio::awaitable<bool> recv_exact(const std::shared_ptr<ClientState>& client,
                                        void* buffer,
                                        std::size_t total) {
    char* ptr = static_cast<char*>(buffer);
    std::size_t received = 0;

    while (received < total) {
        // Consumir primero lo que ya tenemos en stream_buffer
        const std::size_t available = client->stream_buffer.size() - client->stream_offset;
        if (available > 0) {
            const std::size_t chunk = std::min(total - received, available);
            std::memcpy(ptr + received,
                        client->stream_buffer.data() + client->stream_offset,
                        chunk);

            client->stream_offset += chunk;
            received += chunk;

            // Compactar si ya se ha consumido todo
            if (client->stream_offset == client->stream_buffer.size()) {
                client->stream_buffer.clear();
                client->stream_offset = 0;
            }

            continue;
        }

        auto receive_result = co_await client->connection->receive();
        if (!receive_result) {
            // Closed connection
            co_return false;
        }

        auto& data = receive_result->data();
        if (data.empty()) {
            // Closed connection
            co_return false;
        }

        client->stream_buffer.resize(data.size());
        std::memcpy(client->stream_buffer.data(), data.data(), data.size());
        client->stream_offset = 0;
    }

    // End
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

int main(int argc, char* argv[]) {

    // Check args
    if (argc < 2 || argc > 4) {
        std::cerr << "Uso: " << argv[0] << " <ip_servidor> [puerto] [nombre_salida]\n";
        return 1;
    }

    std::string server_ip = argv[1];
    int port = DEFAULT_PORT;
    std::string custom_output_name;

    // Check port
    if (argc >= 3) {
        port = std::stoi(argv[2]);
        if (port <= 0 || port > 65535) {
            std::cerr << "Puerto invalido.\n";
            return 1;
        }
    }

    // Custom output
    if (argc == 4) {
        custom_output_name = argv[3];
    }

    try {
        asio::io_context io_context;
        std::exception_ptr eptr;

        asio::co_spawn(
            io_context,
            [&io_context,
             &eptr,
             server_ip = std::move(server_ip),
             port,
             custom_output_name = std::move(custom_output_name)]() mutable
            -> asio::awaitable<void> {
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
                        throw std::runtime_error("Error en connect: " + init_result.error().message());
                    }

                    auto client = std::make_shared<ClientState>(std::move(*init_result));
                    client->custom_output_name = std::move(custom_output_name);

                    // Read filename size
                    client->filename_size = co_await read_u32(client);
                    if (client->filename_size == 0 || client->filename_size > 4096) {
                        throw std::runtime_error("Tamano de nombre invalido.");
                    }

                    // Read filename
                    client->filename.resize(client->filename_size);
                    if (!(co_await recv_exact(client, client->filename.data(), client->filename_size))) {
                        throw std::runtime_error("Error leyendo nombre de fichero.");
                    }

                    // Read file size
                    client->file_size = co_await read_u64(client);
                    client->remaining = client->file_size;

                    // Output filename
                    const std::string& output_name =
                        client->custom_output_name.empty() ? client->filename : client->custom_output_name;

                    // Open output file
                    client->out.open(output_name, std::ios::binary);
                    if (!client->out) {
                        throw std::runtime_error("No se pudo crear el fichero de salida: " + output_name);
                    }

                    // Main receive loop
                    while (client->remaining > 0) {
                        // Consumir primero lo que ya hubiera en stream_buffer
                        const std::size_t available = client->stream_buffer.size() - client->stream_offset;
                        if (available > 0) {
                            const std::size_t chunk = static_cast<std::size_t>(
                                std::min<std::uint64_t>(client->remaining, available)
                            );

                            client->out.write(client->stream_buffer.data() + client->stream_offset,
                                              static_cast<std::streamsize>(chunk));
                            if (!client->out) {
                                throw std::runtime_error("Error escribiendo el fichero de salida.");
                            }

                            client->stream_offset += chunk;
                            client->remaining -= static_cast<std::uint64_t>(chunk);

                            if (client->stream_offset == client->stream_buffer.size()) {
                                client->stream_buffer.clear();
                                client->stream_offset = 0;
                            }

                            continue;
                        }

                        auto receive_result = co_await client->connection->receive();
                        if (!receive_result) {
                            throw std::runtime_error("Conexion cerrada antes de terminar la descarga.");
                        }

                        auto& data = receive_result->data();
                        if (data.empty()) {
                            throw std::runtime_error("Conexion cerrada antes de terminar la descarga.");
                        }

                        const std::size_t chunk = static_cast<std::size_t>(
                            std::min<std::uint64_t>(client->remaining, data.size())
                        );

                        // Direct write to file
                        client->out.write(reinterpret_cast<const char*>(data.data()),
                                          static_cast<std::streamsize>(chunk));
                        if (!client->out) {
                            throw std::runtime_error("Error escribiendo el fichero de salida.");
                        }

                        client->remaining -= static_cast<std::uint64_t>(chunk);

                        // Guardar sobrante, si existe
                        if (chunk < data.size()) {
                            const std::size_t leftover = data.size() - chunk;
                            client->stream_buffer.resize(leftover);
                            std::memcpy(client->stream_buffer.data(), data.data() + chunk, leftover);
                            client->stream_offset = 0;
                        }
                    }

                    client->out.close();
                    co_await client->connection->close();

                } catch (...) {
                    eptr = std::current_exception();
                }
            },
            asio::detached
        );

        io_context.run();

        if (eptr) {
            std::rethrow_exception(eptr);
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}