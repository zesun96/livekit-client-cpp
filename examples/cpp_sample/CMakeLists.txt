# cmake needs this line
cmake_minimum_required(VERSION 3.8)

project(cpp_sample_project)

add_executable(cpp_sample sample.cpp)

target_link_libraries(cpp_sample PRIVATE livekitclient)
