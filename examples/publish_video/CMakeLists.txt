add_executable(publish_video publish_video.cpp)
target_include_directories(publish_video PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../common)
target_link_libraries(publish_video PRIVATE livekitclient)
