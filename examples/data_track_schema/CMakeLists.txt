add_executable(data_track_schema data_track_schema.cpp)
target_include_directories(data_track_schema PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../common)
target_link_libraries(data_track_schema PRIVATE livekit_example_common)
