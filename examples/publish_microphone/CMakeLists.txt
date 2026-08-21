add_executable(publish_microphone publish_microphone.cpp)
target_include_directories(publish_microphone PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../common)
target_link_libraries(publish_microphone PRIVATE livekitclient)
