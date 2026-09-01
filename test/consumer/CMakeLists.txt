cmake_minimum_required(VERSION 3.20)

cmake_policy(SET CMP0091 NEW)
set(LKC_MSVC_RUNTIME "static" CACHE STRING
    "MSVC runtime linkage: static (/MT, /MTd) or dynamic (/MD, /MDd)")
set_property(CACHE LKC_MSVC_RUNTIME PROPERTY STRINGS static dynamic)
if(LKC_MSVC_RUNTIME STREQUAL "static")
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(LKC_MSVC_RUNTIME STREQUAL "dynamic")
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
else()
  message(FATAL_ERROR "LKC_MSVC_RUNTIME must be static or dynamic")
endif()

set(LIVEKIT_CLIENT_CPP_SOURCE_DIR "" CACHE PATH
    "Optional path to the livekit-client-cpp source tree")
if(LIVEKIT_CLIENT_CPP_SOURCE_DIR)
  set(VCPKG_MANIFEST_DIR "${LIVEKIT_CLIENT_CPP_SOURCE_DIR}" CACHE PATH
      "Directory containing the vcpkg manifest" FORCE)
else()
  set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE PATH
      "Directory containing the vcpkg manifest" FORCE)
endif()
project(livekit-consumer-smoke LANGUAGES C CXX)

if(LIVEKIT_CLIENT_CPP_SOURCE_DIR)
  if(NOT EXISTS "${LIVEKIT_CLIENT_CPP_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
      "LIVEKIT_CLIENT_CPP_SOURCE_DIR does not contain livekit-client-cpp")
  endif()
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_TEST OFF CACHE BOOL "" FORCE)
  set(BUILD_FUNCTIONAL_TESTS OFF CACHE BOOL "" FORCE)
  set(BUILD_INTEGRATION_TESTS OFF CACHE BOOL "" FORCE)
  add_subdirectory("${LIVEKIT_CLIENT_CPP_SOURCE_DIR}" livekit-client-cpp EXCLUDE_FROM_ALL)
  set(LKC_CONSUMER_TARGET livekitclient)
else()
  find_package(LiveKitClient CONFIG REQUIRED)
  set(LKC_CONSUMER_TARGET LiveKitClient::livekitclient)
endif()

add_executable(livekit_consumer_smoke main.cpp)
target_compile_features(livekit_consumer_smoke PRIVATE cxx_std_20)
target_link_libraries(livekit_consumer_smoke PRIVATE ${LKC_CONSUMER_TARGET})

add_executable(livekit_c_consumer_smoke main.c)
set_target_properties(livekit_c_consumer_smoke PROPERTIES LINKER_LANGUAGE CXX)
target_compile_features(livekit_c_consumer_smoke PRIVATE c_std_11)
target_link_libraries(livekit_c_consumer_smoke PRIVATE ${LKC_CONSUMER_TARGET})
