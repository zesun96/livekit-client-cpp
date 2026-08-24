cmake_minimum_required(VERSION 3.20)

set(LIVEKIT_CLIENT_CPP_SOURCE_DIR "" CACHE PATH "Path to the livekit-client-cpp source tree")
if(NOT EXISTS "${LIVEKIT_CLIENT_CPP_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Set LIVEKIT_CLIENT_CPP_SOURCE_DIR to the livekit-client-cpp source tree")
endif()
set(VCPKG_MANIFEST_DIR "${LIVEKIT_CLIENT_CPP_SOURCE_DIR}" CACHE PATH
    "Directory containing the SDK vcpkg manifest" FORCE)
if(MSVC OR CMAKE_GENERATOR MATCHES "Visual Studio")
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

project(livekit-consumer-smoke LANGUAGES CXX)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TEST OFF CACHE BOOL "" FORCE)
set(BUILD_FUNCTIONAL_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_INTEGRATION_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${LIVEKIT_CLIENT_CPP_SOURCE_DIR}" livekit-client-cpp EXCLUDE_FROM_ALL)

add_executable(livekit_consumer_smoke main.cpp)
target_compile_features(livekit_consumer_smoke PRIVATE cxx_std_20)
target_link_libraries(livekit_consumer_smoke PRIVATE livekitclient)
