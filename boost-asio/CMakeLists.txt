cmake_minimum_required(VERSION 3.10)

project("BOOST ASIO" VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -pedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -Wall -Wextra -pedantic")

find_package(Boost REQUIRED COMPONENTS system)

option(BOOST_ASIO_ENABLE_BENCHMARKS "Enable Google Benchmark benchmarks" ON)

add_subdirectory(tcpserver)
add_subdirectory(tcpclient)

if(BOOST_ASIO_ENABLE_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()