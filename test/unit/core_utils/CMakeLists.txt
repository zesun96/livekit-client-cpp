cmake_minimum_required(VERSION 3.14)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(livekit_core_utils_tests LANGUAGES CXX)
  enable_testing()
  get_filename_component(LIVEKIT_CLIENT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
  include(${LIVEKIT_CLIENT_ROOT}/cmake/Testing.cmake)
else()
  set(LIVEKIT_CLIENT_ROOT "${PROJECT_SOURCE_DIR}")
endif()

add_executable(
  livekit_core_utils_tests
  websocket_data_test.cpp
  uri_test.cpp
  async_utils_test.cpp
  audio_gain_test.cpp
  frame_queue_test.cpp
  data_channel_backpressure_test.cpp
  data_track_packet_test.cpp
  reconnect_policy_test.cpp
  signal_url_test.cpp
  token_source_test.cpp
  ${LIVEKIT_CLIENT_ROOT}/src/core/detail/signal_url.cpp
  ${LIVEKIT_CLIENT_ROOT}/src/core/detail/uri.cpp
  ${LIVEKIT_CLIENT_ROOT}/src/core/detail/websocket_uri.cpp
  ${LIVEKIT_CLIENT_ROOT}/src/core/detail/websocket_data.cpp
  ${LIVEKIT_CLIENT_ROOT}/src/core/detail/debouncer.cpp
  ${LIVEKIT_CLIENT_ROOT}/src/core/detail/data_channel_backpressure.cpp
  ${LIVEKIT_CLIENT_ROOT}/src/core/detail/data_track_packet.cpp
  ${LIVEKIT_CLIENT_ROOT}/src/core/option/reconnect_policy.cpp
  ${LIVEKIT_CLIENT_ROOT}/src/core/token_source.cpp
  ${LIVEKIT_CLIENT_ROOT}/src/capture/audio_gain.cpp
  ${LIVEKIT_CLIENT_ROOT}/src/capture/frame_queue.cpp
)

target_compile_features(livekit_core_utils_tests PRIVATE cxx_std_20)
target_include_directories(
  livekit_core_utils_tests
  PRIVATE
    ${LIVEKIT_CLIENT_ROOT}/include
    ${LIVEKIT_CLIENT_ROOT}/src/core/detail
    ${LIVEKIT_CLIENT_ROOT}/src/capture
)
target_link_libraries(livekit_core_utils_tests PRIVATE GTest::gtest_main)

gtest_discover_tests(
  livekit_core_utils_tests
  TEST_PREFIX "unit."
  PROPERTIES LABELS "unit" TIMEOUT 10
)
