/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * TCP file server with Asio
 * Minimal output version for performance and energy measurements.
 */

#include <csignal>

#include <asio.hpp>

#include <arpa/inet.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Alias std::filesystem
namespace fs = std::filesystem;
using tcp = asio::ip::tcp;

// Puerto por defecto
constexpr int DEFAULT_PORT = 8080;

// Conexiones tope
constexpr int BACKLOG = 128;

// Estructura estado cliente
struct ClientState {
    tcp::socket socket;
    std::size_t sent = 0;

    explicit ClientState(asio::io_context& io_context)
        : socket(io_context) {}
};

static void append_u32(std::vector<char>& buffer, std::uint32_t value) {
    std::uint32_t net = htonl(value);
    const char* p = reinterpret_cast<const char*>(&net);
    buffer.insert(buffer.end(), p, p + sizeof(net));
}

static void append_u64(std::vector<char>& buffer, std::uint64_t value) {
    std::uint32_t high = htonl(static_cast<std::uint32_t>(value >> 32));
    std::uint32_t low  = htonl(static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));

    const char* p1 = reinterpret_cast<const char*>(&high);
    const char* p2 = reinterpret_cast<const char*>(&low);

    buffer.insert(buffer.end(), p1, p1 + sizeof(high));
    buffer.insert(buffer.end(), p2, p2 + sizeof(low));
}

static std::vector<char> load_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("No se pudo abrir el fichero: " + path.string());
    }

    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
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

static asio::awaitable<void> do_write(std::shared_ptr<ClientState> client,
                                      std::shared_ptr<const std::vector<char>> packet) {
    try {
        while (client->sent < packet->size()) {
            std::size_t n = co_await client->socket.async_write_some(
                asio::buffer(packet->data() + client->sent, packet->size() - client->sent),
                asio::use_awaitable
            );

            client->sent += n;
        }
    } catch (...) {
    }

    std::error_code close_ec;
    client->socket.shutdown(tcp::socket::shutdown_both, close_ec);
    client->socket.close(close_ec);

    co_return;
}

static asio::awaitable<void> do_accept(asio::io_context& io_context,
                                       tcp::acceptor& acceptor,
                                       std::shared_ptr<const std::vector<char>> packet) {
    while (true) {
        auto client = std::make_shared<ClientState>(io_context);

        try {
            co_await acceptor.async_accept(client->socket, asio::use_awaitable);

            asio::co_spawn(
                io_context,
                do_write(client, packet),
                asio::detached
            );
        } catch (...) {
        }
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGPIPE, SIG_IGN);

    if (argc < 2 || argc > 4) {
        std::cerr << "Uso: " << argv[0] << " <ruta_fichero> [puerto] [num_hebras]\n";
        return 1;
    }

    const fs::path file_path = argv[1];
    int port = DEFAULT_PORT;
    int threads = 1;

    if (argc >= 3) {
        port = std::stoi(argv[2]);
        if (port <= 0 || port > 65535) {
            std::cerr << "Puerto invalido.\n";
            return 1;
        }
    }

    if (argc == 4) {
        threads = std::stoi(argv[3]);
        if (threads <= 0) {
            std::cerr << "Numero de hebras invalido.\n";
            return 1;
        }
    }

    if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
        std::cerr << "El fichero no existe o no es regular: " << file_path << "\n";
        return 1;
    }

    std::vector<char> file_data;
    try {
        file_data = load_file(file_path);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    const std::string filename = file_path.filename().string();
    auto packet = std::make_shared<const std::vector<char>>(build_packet(filename, file_data));

    try {
        asio::io_context io_context;
        auto work_guard = asio::make_work_guard(io_context);

        std::error_code ec;
        tcp::acceptor acceptor(io_context);

        acceptor.open(tcp::v4(), ec);
        if (ec) {
            std::cerr << "open: " << ec.message() << "\n";
            return 1;
        }

        acceptor.set_option(asio::socket_base::reuse_address(true), ec);
        if (ec) {
            std::cerr << "setsockopt: " << ec.message() << "\n";
            return 1;
        }

        tcp::endpoint endpoint(tcp::v4(), static_cast<unsigned short>(port));

        acceptor.bind(endpoint, ec);
        if (ec) {
            std::cerr << "bind: " << ec.message() << "\n";
            return 1;
        }

        acceptor.listen(BACKLOG, ec);
        if (ec) {
            std::cerr << "listen: " << ec.message() << "\n";
            return 1;
        }

        std::cout << "Server ready on port " << port
                  << " with " << threads << " threads\n";

        asio::co_spawn(
            io_context,
            do_accept(io_context, acceptor, packet),
            asio::detached
        );

        std::vector<std::thread> pool;
        pool.reserve(static_cast<std::size_t>(threads > 1 ? threads - 1 : 0));

        for (int i = 1; i < threads; ++i) {
            pool.emplace_back([&io_context]() {
                io_context.run();
            });
        }

        io_context.run();

        for (auto& t : pool) {
            t.join();
        }

    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}