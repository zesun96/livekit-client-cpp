add_executable(livekit_vld_self_test vld_self_test.cpp)
target_compile_features(livekit_vld_self_test PRIVATE cxx_std_20)
lkc_enable_vld(livekit_vld_self_test)

add_executable(livekit_vld_sdk_smoke vld_sdk_smoke.cpp)
target_compile_features(livekit_vld_sdk_smoke PRIVATE cxx_std_20)
target_link_libraries(livekit_vld_sdk_smoke PRIVATE livekitclient)
lkc_enable_vld(livekit_vld_sdk_smoke)
add_custom_command(TARGET livekit_vld_sdk_smoke POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:livekitclient>"
          "$<TARGET_FILE_DIR:livekit_vld_sdk_smoke>"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:${LKC_LIBWEBSOCKETS_TARGET}>"
          "$<TARGET_FILE_DIR:livekit_vld_sdk_smoke>"
  VERBATIM
)

add_test(NAME memory.VisualLeakDetectorSelfTest COMMAND livekit_vld_self_test)
set_tests_properties(memory.VisualLeakDetectorSelfTest PROPERTIES
  LABELS memory
  SKIP_RETURN_CODE 77
  TIMEOUT 30
)

add_test(NAME memory.SdkLifecycle COMMAND livekit_vld_sdk_smoke)
set_tests_properties(memory.SdkLifecycle PROPERTIES
  LABELS memory
  SKIP_RETURN_CODE 77
  TIMEOUT 30
)
