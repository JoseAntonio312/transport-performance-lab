add_executable(tcpserver server.cpp)

target_link_libraries(tcpserver
    PRIVATE
        TAPS::static
        Threads::Threads
)

target_compile_features(tcpserver PRIVATE cxx_std_23)