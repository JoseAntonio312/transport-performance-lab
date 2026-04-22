cmake_minimum_required(VERSION 3.10)

project("TAPS TCP" VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TAPS_TCP_USE_LIBCXX "Build with libc++ when using Clang" OFF)
option(TAPS_ENABLE_BENCHMARKS "Enable Google Benchmark benchmarks" ON)

set(benchmark_DIR "" CACHE PATH "Path to benchmarkConfig.cmake directory")
set(TAPS_ROOT "" CACHE PATH "Path to the installed TAPS prefix")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "CXX compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "CXX compiler ID: ${CMAKE_CXX_COMPILER_ID}")

find_package(Threads REQUIRED)

if(NOT TAPS_ROOT)
    message(FATAL_ERROR "TAPS_ROOT must be set to the TAPS installation prefix.")
endif()

find_path(TAPS_INCLUDE_DIR
    NAMES taps/taps_api.h
    HINTS
        "${TAPS_ROOT}/include"
    REQUIRED
)

find_library(TAPS_LIBRARY
    NAMES TAPS taps
    HINTS
        "${TAPS_ROOT}/lib"
        "${TAPS_ROOT}/lib64"
    REQUIRED
)

message(STATUS "Using TAPS include dir: ${TAPS_INCLUDE_DIR}")
message(STATUS "Using TAPS library: ${TAPS_LIBRARY}")

add_library(taps_manual UNKNOWN IMPORTED)
set_target_properties(taps_manual PROPERTIES
    IMPORTED_LOCATION "${TAPS_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${TAPS_INCLUDE_DIR}"
)

set(TAPS_LINK_TARGET taps_manual)

add_compile_options(-Wall -Wextra -pedantic)

add_compile_options(
    $<$<CONFIG:Debug>:-g>
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-DNDEBUG>
)

if(TAPS_TCP_USE_LIBCXX)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-stdlib=libc++)
        add_link_options(-stdlib=libc++)
        message(STATUS "Building with Clang + libc++")
    else()
        message(FATAL_ERROR "TAPS_TCP_USE_LIBCXX=ON requires Clang")
    endif()
endif()

add_subdirectory(tcpserver)
add_subdirectory(tcpclient)

if(TAPS_ENABLE_BENCHMARKS)
    if(benchmark_DIR)
        message(STATUS "Using explicit benchmark_DIR: ${benchmark_DIR}")
    endif()

    find_package(benchmark REQUIRED CONFIG)

    if(TARGET benchmark::benchmark)
        message(STATUS "Found target benchmark::benchmark")
    elseif(TARGET benchmark::benchmark_main)
        message(STATUS "Found target benchmark::benchmark_main")
    else()
        message(FATAL_ERROR "Google Benchmark was found, but no expected benchmark targets are available.")
    endif()

    add_subdirectory(benchmarks)
endif()