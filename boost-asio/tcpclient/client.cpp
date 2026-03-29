/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * TCP file client with Boost.Asio coroutines
 * Minimal output version for performance and energy measurements.
 */

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <arpa/inet.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Alias boost::asio
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

// Puerto por defecto
constexpr int DEFAULT_PORT = 8080;

// Buffer de recepcion
constexpr std::size_t BUFFER_SIZE = 8192;

// Estructura estado cliente
struct ClientState {
    tcp::socket socket;               // socket descriptor
    std::vector<char> buffer;         // reusable buffer
    std::ofstream out;                // output file
    std::string filename;             // filename
    std::string custom_output_name;   // custom output
    std::uint64_t file_size = 0;      // file size
    std::uint64_t remaining = 0;      // remaining bytes
    std::uint32_t filename_size = 0;  // filename size
    std::uint32_t high_net = 0;       // high uint32
    std::uint32_t low_net = 0;        // low uint32

    explicit ClientState(asio::any_io_executor executor)
        : socket(executor), buffer(BUFFER_SIZE) {}
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
        // Main receive loop
        std::size_t chunk = static_cast<std::size_t>(
            client->remaining > client->buffer.size() ? client->buffer.size() : client->remaining
        );

        std::size_t bytes_transferred =
            co_await asio::async_read(
                client->socket,
                asio::buffer(client->buffer.data(), chunk),
                asio::use_awaitable
            );

        if (bytes_transferred != chunk) {
            throw std::runtime_error("Conexion cerrada antes de terminar la descarga.");
        }

        // Direct write to file
        client->out.write(client->buffer.data(), static_cast<std::streamsize>(bytes_transferred));
        if (!client->out) {
            throw std::runtime_error("Error escribiendo el fichero de salida.");
        }

        client->remaining -= static_cast<std::uint64_t>(bytes_transferred);
    }

    client->out.close();

    boost::system::error_code close_ec;
    client->socket.shutdown(tcp::socket::shutdown_both, close_ec);
    client->socket.close(close_ec);

    co_return;
}

// Async file size read
static asio::awaitable<void> do_read_file_size_low(const std::shared_ptr<ClientState>& client) {
    std::size_t bytes_transferred =
        co_await asio::async_read(
            client->socket,
            asio::buffer(&client->low_net, sizeof(client->low_net)),
            asio::use_awaitable
        );

    if (bytes_transferred != sizeof(client->low_net)) {
        throw std::runtime_error("Error leyendo uint64.");
    }

    // Read file size
    client->file_size = read_u64(client->high_net, client->low_net);
    client->remaining = client->file_size;

    // Output filename
    const std::string& output_name =
        client->custom_output_name.empty() ? client->filename : client->custom_output_name;

    // Open output file
    client->out.open(output_name, std::ios::binary);
    if (!client->out) {
        throw std::runtime_error("No se pudo crear el fichero de salida: " + output_name);
    }

    co_await do_read_content(client);
}

// Async file size read
static asio::awaitable<void> do_read_file_size_high(const std::shared_ptr<ClientState>& client) {
    std::size_t bytes_transferred =
        co_await asio::async_read(
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

    std::size_t bytes_transferred =
        co_await asio::async_read(
            client->socket,
            asio::buffer(client->filename.data(), client->filename_size),
            asio::use_awaitable
        );

    if (bytes_transferred != client->filename_size) {
        throw std::runtime_error("Error leyendo nombre de fichero.");
    }

    co_await do_read_file_size_high(client);
}

// Async filename size read
static asio::awaitable<void> do_read_filename_size(const std::shared_ptr<ClientState>& client) {
    std::size_t bytes_transferred =
        co_await asio::async_read(
            client->socket,
            asio::buffer(&client->filename_size, sizeof(client->filename_size)),
            asio::use_awaitable
        );

    if (bytes_transferred != sizeof(client->filename_size)) {
        throw std::runtime_error("Error leyendo uint32.");
    }

    // Read filename size
    client->filename_size = read_u32(client->filename_size);
    if (client->filename_size == 0 || client->filename_size > 4096) {
        throw std::runtime_error("Tamano de nombre invalido.");
    }

    co_await do_read_filename(client);
}

static asio::awaitable<void> run_client(std::string server_ip,
                                        int port,
                                        std::string custom_output_name) {
    auto executor = co_await asio::this_coro::executor;

    auto client = std::make_shared<ClientState>(executor);
    client->custom_output_name = std::move(custom_output_name);

    // Socket config
    tcp::endpoint server(asio::ip::make_address(server_ip), static_cast<unsigned short>(port));

    // Connect
    co_await client->socket.async_connect(server, asio::use_awaitable);

    co_await do_read_filename_size(client);
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

        asio::co_spawn(
            io_context,
            run_client(std::move(server_ip), port, std::move(custom_output_name)),
            asio::detached
        );

        io_context.run();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}