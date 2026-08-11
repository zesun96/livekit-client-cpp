add_executable(livekit_public_api_tests public_api_test.cpp)
target_compile_features(livekit_public_api_tests PRIVATE cxx_std_20)
target_link_libraries(livekit_public_api_tests PRIVATE livekitclient GTest::gtest_main)

gtest_discover_tests(
  livekit_public_api_tests
  TEST_PREFIX "functional."
  PROPERTIES LABELS "functional" TIMEOUT 30
)
