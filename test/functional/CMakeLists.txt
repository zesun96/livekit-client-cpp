add_executable(
  livekit_public_api_tests
  c_api_test.cpp
  data_track_reader_test.cpp
  data_track_schema_test.cpp
  e2ee_manager_test.cpp
  frame_metadata_test.cpp
  key_provider_test.cpp
  logging_test.cpp
  media_stream_test.cpp
  microphone_source_test.cpp
  participant_state_test.cpp
  preconnect_audio_buffer_test.cpp
  public_api_test.cpp
  video_frame_format_test.cpp
  video_frame_converter_test.cpp
  track_stats_test.cpp
  tracing_test.cpp
  video_codec_factory_test.cpp
  video_encoding_test.cpp
  webrtc_audio_processor_test.cpp
)
target_compile_features(livekit_public_api_tests PRIVATE cxx_std_20)
target_compile_definitions(livekit_public_api_tests PRIVATE
  LIVEKIT_TEST_RESOURCE_DIR="${PROJECT_SOURCE_DIR}/resources"
)
target_link_libraries(livekit_public_api_tests PRIVATE livekitclient GTest::gtest_main)
target_include_directories(livekit_public_api_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src/core/detail
  ${PROJECT_SOURCE_DIR}/src/core/e2ee
  ${PROJECT_SOURCE_DIR}/src/core/track
  ${PROJECT_SOURCE_DIR}/src/capture
)

gtest_discover_tests(
  livekit_public_api_tests
  TEST_PREFIX "functional."
  PROPERTIES LABELS "functional" TIMEOUT 30
)

add_executable(livekit_track_recorder_tests track_recorder_test.cpp)
target_compile_features(livekit_track_recorder_tests PRIVATE cxx_std_20)
target_link_libraries(livekit_track_recorder_tests PRIVATE livekitclient GTest::gtest_main)
gtest_discover_tests(
  livekit_track_recorder_tests
  TEST_PREFIX "functional."
  PROPERTIES LABELS "functional;recording" TIMEOUT 30
  DISCOVERY_MODE PRE_TEST
)

add_executable(livekit_c_api_smoke_tests ../consumer/main.c)
set_target_properties(livekit_c_api_smoke_tests PROPERTIES LINKER_LANGUAGE CXX)
target_compile_features(livekit_c_api_smoke_tests PRIVATE c_std_11)
target_link_libraries(livekit_c_api_smoke_tests PRIVATE livekitclient)
add_test(NAME functional.CApiConsumerSmoke COMMAND livekit_c_api_smoke_tests)
set_tests_properties(functional.CApiConsumerSmoke PROPERTIES LABELS "functional" TIMEOUT 30)
