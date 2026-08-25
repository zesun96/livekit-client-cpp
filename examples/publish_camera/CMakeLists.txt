add_executable(publish_camera publish_camera.cpp)
target_include_directories(publish_camera PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../common)
target_link_libraries(publish_camera PRIVATE livekit_example_common)
