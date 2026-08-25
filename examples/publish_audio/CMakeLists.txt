cmake_minimum_required(VERSION 3.8)

project(publish_audio_project)

add_executable(publish_audio publish_audio.cpp)

target_include_directories(
  publish_audio
  PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../common
)
 
target_link_libraries(publish_audio PRIVATE livekit_example_common)
