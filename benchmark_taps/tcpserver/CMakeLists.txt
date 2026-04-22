add_executable(tcpserver server.cpp)

target_link_libraries(tcpserver
    PRIVATE
        ${TAPS_LINK_TARGET}
        Threads::Threads
)

target_compile_features(tcpserver PRIVATE cxx_std_23)