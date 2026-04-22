add_executable(tcpclient client.cpp)

target_link_libraries(tcpclient
    PRIVATE
        ${TAPS_LINK_TARGET}
        Threads::Threads
)

target_compile_features(tcpclient PRIVATE cxx_std_23)