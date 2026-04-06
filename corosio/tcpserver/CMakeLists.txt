add_executable(tcpserver server.cpp)
target_link_libraries(tcpserver PRIVATE Boost::corosio)
target_compile_features(tcpserver PRIVATE cxx_std_23)