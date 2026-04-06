/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * TCP file server with Corosio
 * Minimal output version for performance and energy measurements.
 */

#include <csignal>

#include <boost/corosio.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/ex/run_async.hpp>

#include <arpa/inet.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// Alias std::filesystem
namespace fs = std::filesystem;
namespace corosio = boost::corosio;
namespace capy = boost::capy;

// Puerto por defecto
constexpr int DEFAULT_PORT = 8080;

// Conexiones tope
constexpr int BACKLOG = 128;

// Estructura estado cliente
struct ClientState {
    std::size_t sent = 0; // sent bytes
};

// Manejo de la arquitectura de red
static void append_u32(std::vector<char>& buffer, std::uint32_t value) {
    std::uint32_t net = htonl(value);
    const char* p = reinterpret_cast<const char*>(&net);
    buffer.insert(buffer.end(), p, p + sizeof(net));
}

// Añadir un uint64_t al buffer en formato de red
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

// Construcción de nuestro paquete
// Formato:
// [u32 tam_nombre][nombre][u64 tam_fichero][datos]
static std::vector<char> build_packet(const std::string& filename,
                                      const std::vector<char>& file_data) {
    std::vector<char> packet;
    packet.reserve(sizeof(std::uint32_t) + filename.size() +
                   sizeof(std::uint64_t) + file_data.size());

    append_u32(packet, static_cast<std::uint32_t>(filename.size()));
    packet.insert(packet.end(), filename.begin(), filename.end());

    append_u64(packet, static_cast<std::uint64_t>(file_data.size()));
    packet.insert(packet.end(), file_data.begin(), file_data.end());

    return packet;
}

static capy::task<void> serve_client(corosio::tcp_socket sock,
                                     const std::vector<char>& packet) {
    ClientState client;

    while (client.sent < packet.size()) {
        auto [ec, n] = co_await sock.write_some(
            capy::const_buffer(packet.data() + client.sent,
                               packet.size() - client.sent));

        if (ec || n == 0) {
            break;
        }

        client.sent += static_cast<std::size_t>(n);
    }

    sock.close();
    co_return;
}

static capy::task<void> accept_loop(corosio::io_context& ctx,
                                    corosio::tcp_acceptor& acceptor,
                                    const std::vector<char>& packet) {
    while (true) {
        corosio::tcp_socket sock(ctx);
        auto [ec] = co_await acceptor.accept(sock);

        if (ec) {
            continue;
        }

        capy::run_async(ctx.get_executor())(
            serve_client(std::move(sock), packet)
        );
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGPIPE, SIG_IGN);

    // Check args
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
    const std::vector<char> packet = build_packet(filename, file_data);

    corosio::io_context ctx;
    corosio::tcp_acceptor acceptor(ctx);

    acceptor.open(corosio::tcp::v4());

    auto bind_ec = acceptor.bind(corosio::endpoint(static_cast<std::uint16_t>(port)));
    if (bind_ec) {
        std::cerr << "bind: " << bind_ec.message() << "\n";
        return 1;
    }

    auto listen_ec = acceptor.listen(BACKLOG);
    if (listen_ec) {
        std::cerr << "listen: " << listen_ec.message() << "\n";
        return 1;
    }

    std::cout << "Server ready on port " << port
              << " with " << threads << " threads\n";

    capy::run_async(ctx.get_executor())(
        accept_loop(ctx, acceptor, packet)
    );

    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(threads > 1 ? threads - 1 : 0));

    for (int i = 1; i < threads; ++i) {
        pool.emplace_back([&ctx]() {
            ctx.run();
        });
    }

    ctx.run();

    for (auto& t : pool) {
        t.join();
    }

    return 0;
}