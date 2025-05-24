cmake_minimum_required(VERSION 3.8)

project(room_event_project)

add_executable(room_event room_event.cpp)

target_link_libraries(room_event PRIVATE livekitclient)