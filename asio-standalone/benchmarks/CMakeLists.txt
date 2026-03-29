find_package(Threads REQUIRED)
find_package(benchmark REQUIRED)

add_executable(bench_tcp bench_tcp.cpp)

target_compile_features(bench_tcp PRIVATE cxx_std_23)

target_compile_definitions(bench_tcp
    PRIVATE
        ASIO_STANDALONE
)

target_include_directories(bench_tcp
    PRIVATE
        ${CMAKE_SOURCE_DIR}/external/asio/asio/include
)

target_link_libraries(bench_tcp
    PRIVATE
        benchmark::benchmark
        benchmark::benchmark_main
        Threads::Threads
)