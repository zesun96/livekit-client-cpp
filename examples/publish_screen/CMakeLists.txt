add_executable(publish_screen publish_screen.cpp)
target_include_directories(publish_screen PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../common)
target_link_libraries(publish_screen PRIVATE livekit_example_common)
