cmake_minimum_required(VERSION 3.8)

project(room_event_project)

add_executable(room_event room_event.cpp)

target_include_directories(room_event PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../common)
target_link_libraries(room_event PRIVATE livekitclient)
