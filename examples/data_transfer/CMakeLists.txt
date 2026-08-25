add_executable(data_transfer data_transfer.cpp)
target_include_directories(data_transfer PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../common)
target_link_libraries(data_transfer PRIVATE livekit_example_common)
