add_executable(rpc rpc.cpp)
target_include_directories(rpc PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../common)
target_link_libraries(rpc PRIVATE livekit_example_common)
