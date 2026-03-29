add_executable(bench_tcp bench_tcp.cpp)

target_compile_features(bench_tcp PRIVATE cxx_std_23)

target_link_libraries(bench_tcp
    PRIVATE
        benchmark::benchmark
        benchmark::benchmark_main
        TAPS::taps_static
        Threads::Threads
)