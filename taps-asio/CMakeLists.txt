cmake_minimum_required(VERSION 3.20)

project(taps_asio_tcp LANGUAGES CXX)

option(TAPS_ENABLE_BENCHMARKS "Build benchmarks" ON)
option(TAPS_TCP_USE_LIBCXX "Use libc++ with Clang" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED TAPS_ROOT)
    message(FATAL_ERROR "TAPS_ROOT is not defined. Pass -DTAPS_ROOT=/path/to/proyecto_taps/install-gcc or install-clang")
endif()

set(TAPS_INCLUDE_DIR "${TAPS_ROOT}/include")
set(TAPS_LIBRARY "${TAPS_ROOT}/lib/libtaps.a")

if(NOT EXISTS "${TAPS_INCLUDE_DIR}/taps/taps_api.h")
    message(FATAL_ERROR "TAPS header not found: ${TAPS_INCLUDE_DIR}/taps/taps_api.h")
endif()

if(NOT EXISTS "${TAPS_LIBRARY}")
    message(FATAL_ERROR "TAPS library not found: ${TAPS_LIBRARY}")
endif()

message(STATUS "TAPS_ROOT: ${TAPS_ROOT}")
message(STATUS "TAPS_INCLUDE_DIR: ${TAPS_INCLUDE_DIR}")
message(STATUS "TAPS_LIBRARY: ${TAPS_LIBRARY}")

add_library(TAPS::static STATIC IMPORTED GLOBAL)
set_target_properties(TAPS::static PROPERTIES
    IMPORTED_LOCATION "${TAPS_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${TAPS_INCLUDE_DIR}"
    INTERFACE_COMPILE_DEFINITIONS ASIO_STANDALONE
)

find_package(Threads REQUIRED)

if(TAPS_ENABLE_BENCHMARKS)
    find_package(benchmark REQUIRED)
endif()

add_subdirectory(tcpserver)
add_subdirectory(tcpclient)

if(TAPS_ENABLE_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()