# cmake needs this line
cmake_minimum_required(VERSION 3.8)

project(cpp_sample_project)

add_executable(cpp_sample sample.cpp)

target_include_directories(cpp_sample PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../common)
target_link_libraries(cpp_sample PRIVATE livekitclient)
