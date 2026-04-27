find_package(benchmark REQUIRED)

add_executable(bench_tcp bench_tcp.cpp)

target_link_libraries(bench_tcp
    PRIVATE
    benchmark::benchmark
    benchmark::benchmark_main
    Boost::corosio
    pthread
)

target_compile_features(bench_tcp PRIVATE cxx_std_23)