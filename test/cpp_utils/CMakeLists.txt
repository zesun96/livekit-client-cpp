# cmake needs this line
cmake_minimum_required(VERSION 3.8)

project(cpp_util_project)

add_executable(cpp_util main.cpp)

target_link_libraries(cpp_util PRIVATE livekitclient)
