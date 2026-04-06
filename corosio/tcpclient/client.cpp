/*
 * Copyright (c) 2026 José Antonio García Montañez
 *
 * TCP file client with Corosio
 * Minimal output version for performance and energy measurements.
 */

#include <boost/corosio.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/buffers.hpp>

#include <arpa/inet.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// Puerto por defecto
constexpr int DEFAULT_PORT = 8080;

// Buffer de recepcion
constexpr std::size_t BUFFER_SIZE = 8192;

namespace corosio = boost::corosio;
namespace capy = boost::capy;

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

static capy::task<int> run_client(corosio::io_context& ioc,
                                  const std::string& server_ip,
                                  int port,
                                  const std::string& custom_output_name) {
    corosio::tcp_socket sock(ioc);

    sock.open();

    auto [connect_ec] = co_await sock.connect(
        corosio::endpoint(corosio::endpoint(server_ip),
                          static_cast<unsigned short>(port)));

    if (connect_ec) {
        std::cerr << "Error: connect: " << connect_ec.message() << "\n";
        co_return 1;
    }

    try {
        std::uint32_t filename_size = co_await read_u32(sock);
        if (filename_size == 0 || filename_size > 4096) {
            throw std::runtime_error("Tamano de nombre invalido.");
        }

        std::string filename(filename_size, '\0');
        if (!(co_await recv_exact(sock, filename.data(), filename_size))) {
            throw std::runtime_error("Error leyendo nombre de fichero.");
        }

        std::uint64_t file_size = co_await read_u64(sock);

        const std::string& output_name =
            custom_output_name.empty() ? filename : custom_output_name;

        std::ofstream out(output_name, std::ios::binary);
        if (!out) {
            throw std::runtime_error("No se pudo crear el fichero de salida: " + output_name);
        }

        std::vector<char> buffer(BUFFER_SIZE);
        std::uint64_t remaining = file_size;

        while (remaining > 0) {
            std::size_t chunk = static_cast<std::size_t>(
                remaining > buffer.size() ? buffer.size() : remaining
            );

            auto [read_ec, n] = co_await sock.read_some(
                capy::mutable_buffer(buffer.data(), chunk));

            if (read_ec) {
                throw std::runtime_error(std::string("Error en recv: ") + read_ec.message());
            }

            if (n == 0) {
                throw std::runtime_error("Conexion cerrada antes de terminar la descarga.");
            }

            out.write(buffer.data(), static_cast<std::streamsize>(n));
            if (!out) {
                throw std::runtime_error("Error escribiendo el fichero de salida.");
            }

            remaining -= static_cast<std::uint64_t>(n);
        }

        out.close();
        sock.close();
        co_return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        sock.close();
        co_return 1;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 4) {
        std::cerr << "Uso: " << argv[0] << " <ip_servidor> [puerto] [nombre_salida]\n";
        return 1;
    }

    std::string server_ip = argv[1];
    int port = DEFAULT_PORT;
    std::string custom_output_name;

    if (argc >= 3) {
        port = std::stoi(argv[2]);
        if (port <= 0 || port > 65535) {
            std::cerr << "Puerto invalido.\n";
            return 1;
        }
    }

    if (argc == 4) {
        custom_output_name = argv[3];
    }

    try {
        corosio::io_context ioc;
        int result = 1;

        capy::run_async(ioc.get_executor())(
            [&]() -> capy::task<void> {
                result = co_await run_client(ioc, server_ip, port, custom_output_name);
            }()
        );

        ioc.run();
        return result;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}