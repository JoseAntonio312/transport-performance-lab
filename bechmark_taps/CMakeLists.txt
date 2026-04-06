cmake_minimum_required(VERSION 3.10)

project("TAPS TCP" VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TAPS_TCP_USE_LIBCXX "Build with libc++ when using Clang" OFF)
option(TAPS_ENABLE_BENCHMARKS "Enable Google Benchmark benchmarks" ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "CXX compiler: ${CMAKE_CXX_COMPILER}")

find_package(Threads REQUIRED)
find_package(benchmark REQUIRED)
find_package(TAPS REQUIRED CONFIG)

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
    add_subdirectory(benchmarks)
endif()