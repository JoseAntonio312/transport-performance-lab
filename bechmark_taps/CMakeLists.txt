cmake_minimum_required(VERSION 3.10)

project("TAPS TCP" VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -pedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -Wall -Wextra -pedantic")

# Threads
find_package(Threads REQUIRED)

# Google Benchmark
find_package(benchmark REQUIRED)

# Ruta donde instalaste TAPS
# Ajusta esta ruta si tu install esta en otro sitio
set(CMAKE_PREFIX_PATH
    "/home/jagarcia/Escritorio/TFM/taps-asio/proyecto_taps/install"
    ${CMAKE_PREFIX_PATH}
)

# Buscar TAPS instalada
find_package(TAPS REQUIRED CONFIG)

option(TAPS_ENABLE_BENCHMARKS "Enable Google Benchmark benchmarks" ON)

add_subdirectory(tcpserver)
add_subdirectory(tcpclient)

if(TAPS_ENABLE_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()