add_executable(livekit_server_integration_tests livekit_server_test.cpp)
target_compile_features(livekit_server_integration_tests PRIVATE cxx_std_20)
target_link_libraries(livekit_server_integration_tests PRIVATE livekitclient GTest::gtest_main)

gtest_discover_tests(
  livekit_server_integration_tests
  TEST_PREFIX "integration."
  PROPERTIES LABELS "integration" TIMEOUT 60 RUN_SERIAL TRUE
)
