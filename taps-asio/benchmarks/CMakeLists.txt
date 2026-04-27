add_executable(bench_tcp bench_tcp.cpp)

target_link_libraries(bench_tcp
    PRIVATE
        TAPS::static
        Threads::Threads
        benchmark::benchmark
)

target_compile_features(bench_tcp PRIVATE cxx_std_23)