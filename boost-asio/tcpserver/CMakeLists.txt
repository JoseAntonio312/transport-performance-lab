find_package(Boost REQUIRED COMPONENTS system)

add_executable(tcpserver server.cpp)
target_compile_features(tcpserver PRIVATE cxx_std_23)

target_link_libraries(tcpserver
    PRIVATE
        Boost::system
        pthread
)