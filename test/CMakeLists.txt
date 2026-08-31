include(${PROJECT_SOURCE_DIR}/cmake/Testing.cmake)

if(LKC_ENABLE_VLD)
  add_subdirectory(vld)
endif()

add_subdirectory(unit)

if(BUILD_FUNCTIONAL_TESTS)
  add_subdirectory(functional)
endif()

if(BUILD_INTEGRATION_TESTS)
  add_subdirectory(integration)
endif()

if(BUILD_LEGACY_TEST_TOOLS)
  add_subdirectory(cpp_utils)
endif()
