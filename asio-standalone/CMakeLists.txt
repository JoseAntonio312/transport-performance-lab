cmake_minimum_required(VERSION 3.10)

project("ASIO STANDALONE" VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -pedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -Wall -Wextra -pedantic")

# Threads (necesario para Asio)
find_package(Threads REQUIRED)

# Ruta a Asio standalone (si no está instalado globalmente)
# Ajusta esto según tu proyecto
set(ASIO_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/external/asio/asio/include")

option(ASIO_ENABLE_BENCHMARKS "Enable Google Benchmark benchmarks" ON)

add_subdirectory(tcpserver)
add_subdirectory(tcpclient)

if(ASIO_ENABLE_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()