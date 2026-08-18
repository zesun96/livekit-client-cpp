add_executable(
  livekit_public_api_tests
  c_api_test.cpp
  microphone_source_test.cpp
  participant_state_test.cpp
  public_api_test.cpp
  screen_capture_adapter_test.cpp
  track_stats_test.cpp
  video_encoding_test.cpp
  webrtc_audio_processor_test.cpp
)
target_compile_features(livekit_public_api_tests PRIVATE cxx_std_20)
target_link_libraries(livekit_public_api_tests PRIVATE livekitclient GTest::gtest_main)
target_include_directories(livekit_public_api_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src/core/detail
  ${PROJECT_SOURCE_DIR}/src/core/track
  ${PROJECT_SOURCE_DIR}/src/capture
)

gtest_discover_tests(
  livekit_public_api_tests
  TEST_PREFIX "functional."
  PROPERTIES LABELS "functional" TIMEOUT 30
)
