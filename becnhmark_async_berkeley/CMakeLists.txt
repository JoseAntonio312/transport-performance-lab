cmake_minimum_required(VERSION 3.16)

project(async_file_transfer
    VERSION 1.0.0
    DESCRIPTION "TCP file transfer and benchmarking with async-berkeley"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ASYNC_TCP_ENABLE_BENCHMARKS "Enable Google Benchmark benchmarks" ON)

# Path to local async-berkeley checkout
set(ASYNC_BERKELEY_DIR
    "/home/jagarcia/Escritorio/TFM/async-berkeley/async-berkeley"
    CACHE PATH "Path to async-berkeley root directory"
)

if(NOT EXISTS "${ASYNC_BERKELEY_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "async-berkeley not found at: ${ASYNC_BERKELEY_DIR}\n"
        "Set -DASYNC_BERKELEY_DIR=/path/to/async-berkeley"
    )
endif()

# Nada de warnings en ningún build type
set(CMAKE_CXX_FLAGS_DEBUG   "-g -O0 -w")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -w")

add_subdirectory("${ASYNC_BERKELEY_DIR}" "${CMAKE_BINARY_DIR}/async-berkeley-build")

add_subdirectory(tcpserver)
add_subdirectory(tcpclient)

if(ASYNC_TCP_ENABLE_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()