add_executable(livekit_server_integration_tests livekit_server_test.cpp)
target_compile_features(livekit_server_integration_tests PRIVATE cxx_std_20)
target_link_libraries(livekit_server_integration_tests PRIVATE livekitclient GTest::gtest_main)

if(OFFICIAL_LIVEKIT_CPP_ROOT)
  find_package(
    LiveKit CONFIG REQUIRED
    PATHS "${OFFICIAL_LIVEKIT_CPP_ROOT}/lib/cmake/LiveKit"
    NO_DEFAULT_PATH
  )
  add_executable(livekit_official_cpp_e2ee_peer official_cpp_e2ee_peer.cpp)
  target_compile_features(livekit_official_cpp_e2ee_peer PRIVATE cxx_std_20)
  target_link_libraries(livekit_official_cpp_e2ee_peer PRIVATE LiveKit::livekit)
  add_custom_command(
    TARGET livekit_official_cpp_e2ee_peer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OFFICIAL_LIVEKIT_CPP_ROOT}/bin/livekit.dll"
            "$<TARGET_FILE_DIR:livekit_official_cpp_e2ee_peer>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OFFICIAL_LIVEKIT_CPP_ROOT}/bin/livekit_ffi.dll"
            "$<TARGET_FILE_DIR:livekit_official_cpp_e2ee_peer>"
  )
endif()

gtest_discover_tests(
  livekit_server_integration_tests
  TEST_PREFIX "integration."
  PROPERTIES LABELS "integration" TIMEOUT 120 RUN_SERIAL TRUE
)
