/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * TCP file server with TAPS
 * Minimal output version for performance and energy measurements.
 */

#include <csignal>

#include "taps/taps_api.h"
#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_awaitable.hpp>

#include <arpa/inet.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// Alias std::filesystem
namespace fs = std::filesystem;
namespace asio = ::asio;

// Puerto por defecto
constexpr int DEFAULT_PORT = 8080;

// Conexiones tope
constexpr int BACKLOG = 128;

// Estructura estado cliente
struct ClientState {
    std::unique_ptr<taps::Connection> connection; // connection descriptor
    std::size_t sent = 0;                         // sent bytes

    explicit ClientState(std::unique_ptr<taps::Connection> conn)
        : connection(std::move(conn)) {}
};

// Manejo de la arquitectura de red
static void append_u32(std::vector<char>& buffer, std::uint32_t value) {
    std::uint32_t net = htonl(value);
    const char* p = reinterpret_cast<const char*>(&net);
    buffer.insert(buffer.end(), p, p + sizeof(net));
}

// Añadir un uint64_t al buffer en formato de red
// Como no usamos una funcion tipo htonll, lo partimos en dos uint32_t
static void append_u64(std::vector<char>& buffer, std::uint64_t value) {
    std::uint32_t high = htonl(static_cast<std::uint32_t>(value >> 32));
    std::uint32_t low  = htonl(static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));

    const char* p1 = reinterpret_cast<const char*>(&high);
    const char* p2 = reinterpret_cast<const char*>(&low);

    buffer.insert(buffer.end(), p1, p1 + sizeof(high));
    buffer.insert(buffer.end(), p2, p2 + sizeof(low));
}

// Cargar el fichero completo en memoria
static std::vector<char> load_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("No se pudo abrir el fichero: " + path.string());
    }

    // Size
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();

    // Back to beginning
    file.seekg(0, std::ios::beg);

    if (size < 0) {
        throw std::runtime_error("No se pudo obtener el tamano del fichero.");
    }

    std::vector<char> data(static_cast<std::size_t>(size));

    if (size > 0 && !file.read(data.data(), size)) {
        throw std::runtime_error("Error leyendo el fichero.");
    }

    return data;
}

// COntrucción de nuestro paquete
// Formato:
// [u32 tam_nombre][nombre][u64 tam_fichero][datos]
static std::vector<char> build_packet(const std::string& filename, const std::vector<char>& file_data) {
    std::vector<char> packet;
    packet.reserve(sizeof(std::uint32_t) + filename.size() +
                   sizeof(std::uint64_t) + file_data.size());

    append_u32(packet, static_cast<std::uint32_t>(filename.size()));
    packet.insert(packet.end(), filename.begin(), filename.end());

    append_u64(packet, static_cast<std::uint64_t>(file_data.size()));
    packet.insert(packet.end(), file_data.begin(), file_data.end());

    return packet;
}

// Async write
static asio::awaitable<void> do_write(std::shared_ptr<ClientState> client,
                                      std::shared_ptr<const std::vector<char>> packet) {
    while (client->sent < packet->size()) {
        // Writing
        std::vector<std::uint8_t> chunk(
            reinterpret_cast<const std::uint8_t*>(packet->data() + client->sent),
            reinterpret_cast<const std::uint8_t*>(packet->data() + packet->size())
        );

        auto message = taps::Message(std::move(chunk));
        auto result = co_await client->connection->send(std::move(message));

        if (!result) {
            // Error
            co_await client->connection->close();
            co_return;
        }

        // Counter update
        client->sent = packet->size();
    }

    // End
    co_await client->connection->close();
}

// Async accept
static asio::awaitable<void> do_accept(taps::Listener& listener,
                                       std::shared_ptr<const std::vector<char>> packet) {
    while (true) {
        //Accept
        auto accept_result = co_await listener.accept();

        //Max clients
        if (!accept_result) {
            continue;
        }

        auto client = std::make_shared<ClientState>(std::move(*accept_result));

        asio::co_spawn(
            co_await asio::this_coro::executor,
            do_write(client, packet),
            asio::detached
        );
    }
}

int main(int argc, char* argv[]) {

    std::signal(SIGPIPE, SIG_IGN);

    // Check args
    if (argc < 2 || argc > 3) {
        std::cerr << "Uso: " << argv[0] << " <ruta_fichero> [puerto]\n";
        return 1;
    }

    const fs::path file_path = argv[1];
    int port = DEFAULT_PORT;

    if (argc == 3) {
        port = std::stoi(argv[2]);
        if (port <= 0 || port > 65535) {
            std::cerr << "Puerto invalido.\n";
            return 1;
        }
    }

    // Check file
    if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
        std::cerr << "El fichero no existe o no es regular: " << file_path << "\n";
        return 1;
    }

    // Load file
    std::vector<char> file_data;
    try {
        file_data = load_file(file_path);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    // Filename
    const std::string filename = file_path.filename().string();

    // Final packet
    auto packet = std::make_shared<const std::vector<char>>(build_packet(filename, file_data));

    try {
        asio::io_context io_context;

        asio::co_spawn(
            io_context,
            [&io_context, port, packet]() -> asio::awaitable<void> {
                taps::TransportServices ts(io_context);

                taps::TransportProperties props;
                props.set(taps::PropertyKey::RELIABILITY, taps::SelectionProperty::REQUIRE);
                props.set(taps::PropertyKey::PRESERVE_ORDER, taps::SelectionProperty::REQUIRE);

                auto listener_result = co_await ts.listen(
                    taps::LocalEndpoint{"0.0.0.0", static_cast<std::uint16_t>(port)},
                    std::move(props)
                );

                if (!listener_result) {
                    std::cerr << listener_result.error().message() << "\n";
                    co_return;
                }

                auto listener = std::move(*listener_result);

                auto listen_result = co_await listener->listen();
                if (!listen_result) {
                    std::cerr << listen_result.error().message() << "\n";
                    co_return;
                }

                std::cout << "Server ready on port " << port << '\n';

                co_await do_accept(*listener, packet);
            },
            asio::detached
        );

        // Loop
        io_context.run();

    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}