# ----------------------------------------------------------------------------
#  Root CMake file for livekit-client-cpp-sdk
#
# ----------------------------------------------------------------------------
cmake_minimum_required (VERSION 3.11)

project ("livekit-client-cpp-sdk")

set(PROJECT_DESCRIPTION "C/C++ livekit sdk")

if(MSVC)
  # For MSVC RUNTIME LIBRARY, need CMP0091=NEW and cmake 3.15+
  cmake_policy(SET CMP0091 NEW)

  # Configure MSVC runtime library settings for Debug and Release builds using MTd
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
endif()

include(GNUInstallDirs)

# C++ standard requirements.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_POLICY_DEFAULT_CMP0048 NEW)

option(USE_SYSTEM_PROTOBUF "Use system protobuf" ON)
option(USE_SYSTEM_JSON "Use system nlohmann/json" OFF)
option(USE_SYSTEM_ABSL "Use system Abseil" OFF)

include(FetchContent)
include(cmake/FindWebRTC.cmake)

if(USE_SYSTEM_ABSL)
  find_package(absl CONFIG REQUIRED)
endif()

if (NOT TARGET plog)
    add_subdirectory(deps/plog EXCLUDE_FROM_ALL)
endif()

find_package(libwebsockets CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

if (WIN32)
  if (DEFINED VCPKG_CMAKE_FIND_ROOT_PATH)
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
      list(GET VCPKG_CMAKE_FIND_ROOT_PATH 0 FIRST_VCPKG_DEBUG_ROOT)
      list(GET VCPKG_CMAKE_FIND_ROOT_PATH 1 FIRST_VCPKG_ROOT)
    else()
      list(GET VCPKG_CMAKE_FIND_ROOT_PATH 0 FIRST_VCPKG_ROOT)
      list(GET VCPKG_CMAKE_FIND_ROOT_PATH 1 FIRST_VCPKG_DEBUG_ROOT)
    endif()
    set(LIBWEBSOCKETS_LIBRARIES ${FIRST_VCPKG_ROOT}/lib/websockets.lib)
    set(LK_VCPKG_LIBRARIES_DIR ${FIRST_VCPKG_ROOT}/lib)
  else()
    SET(LIBWEBSOCKETS_LIBRARIES "websockets.lib")
  endif()
else()
  pkg_check_modules(LIBWEBSOCKETS REQUIRED libwebsockets)
endif()

if(USE_SYSTEM_PROTOBUF)
  find_package(protobuf CONFIG REQUIRED)
else()
  if (NOT TARGET probobuf)
    add_subdirectory(deps/probobuf EXCLUDE_FROM_ALL)
  endif()
endif()


if(USE_SYSTEM_JSON)
  find_package(nlohmann_json 3.2.0 REQUIRED)
else()
  # Typically you don't care so much for a third party library's tests to be
  # run from your own project's code.
  set(JSON_BuildTests OFF CACHE INTERNAL "")
  if (NOT TARGET nlohmann_json)
    add_subdirectory(deps/nlohmann_json)
  endif()
endif()


message(STATUS "libwebrtc source dir: ${libwebrtc_SOURCE_DIR}")
message(STATUS "plog source dir: ${plog_SOURCE_DIR}")
message(STATUS "libwebsockets include dir: ${LIBWEBSOCKETS_INCLUDE_DIRS}")
message(STATUS "libwebsockets lib: ${LIBWEBSOCKETS_LIBRARIES}")
message(STATUS "protobuf include dir: ${PROTOBUF_INCLUDE_DIRS}")
message(STATUS "protobuf libraries: ${PROTOBUF_LIBRARIES}")
message(STATUS "nlohmann_json include dir: ${nlohmann_json_SOURCE_DIR}/single_include")

set(LK_PROTO_PATH protocol/protobufs)
set(LK_PROTO_FILES
    ${LK_PROTO_PATH}/livekit_models.proto
    ${LK_PROTO_PATH}/livekit_metrics.proto
    ${LK_PROTO_PATH}/livekit_rtc.proto
)
set(PROTO_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_BINARY_DIR})

# livekit-proto
add_library(livekit_proto OBJECT ${LK_PROTO_FILES})
target_include_directories(livekit_proto PUBLIC "$<BUILD_INTERFACE:${PROTO_BINARY_DIR}>")
target_include_directories(livekit_proto PUBLIC ${Protobuf_INCLUDE_DIRS})
target_link_libraries(livekit_proto PUBLIC protobuf::libprotobuf)

set(
  LKC_CORE_SOURCE_FILES

  src/core/e2ee/e2ee_manager.cpp
  src/core/e2ee/key_provider.cpp

  src/core/track/audio_track.cpp
  src/core/track/local_audio_track.cpp
  src/core/track/local_publication.cpp
  src/core/track/local_track.cpp
  src/core/track/local_video_track.cpp
  src/core/track/publication.cpp
  src/core/track/remote_audio_track.cpp
  src/core/track/remote_publication.cpp
  src/core/track/remote_track.cpp
  src/core/track/remote_video_track.cpp
  src/core/track/video_track.cpp

  src/core/option/defalut_option.cpp

  src/core/livekit_client.cpp
  src/core/livekit_client_test.cpp
  src/core/local_participant.cpp
  src/core/remote_participant.cpp
  src/core/room.cpp

  src/core/detail/converted_proto.cpp
  src/core/detail/internals.cpp
  src/core/detail/peer_transport.cpp
  src/core/detail/rtc_engine.cpp
  src/core/detail/rtc_session.cpp
  src/core/detail/signal_client.cpp
  src/core/detail/uri.cpp
  src/core/detail/utils.cpp
  src/core/detail/websocket_client.cpp
  src/core/detail/websocket_data.cpp
  src/core/detail/websocket_uri.cpp

)

add_library (livekitclient STATIC

  ${LKC_CORE_SOURCE_FILES}

  ${LK_PROTO_FILES}
)

protobuf_generate(
    TARGET livekit_proto
    PROTOS ${LK_PROTO_FILES} 
    APPEND_PATH ${LK_PROTO_PATH}
    IMPORT_DIRS ${LK_PROTO_PATH}
    PROTOC_OUT_DIR ${PROTO_BINARY_DIR}
)

link_directories(${libwebrtc_SOURCE_DIR}/lib)
link_directories(${LK_VCPKG_LIBRARIES_DIR})

target_include_directories(livekitclient PUBLIC ${libwebrtc_SOURCE_DIR}/include)
target_include_directories(livekitclient PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(livekitclient PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS}/include)
target_include_directories(livekitclient PUBLIC ${libwebrtc_SOURCE_DIR}/include/third_party/abseil-cpp)
target_include_directories(livekitclient PRIVATE ${PROTOBUF_INCLUDE_DIRS})
target_include_directories(livekitclient PUBLIC "$<BUILD_INTERFACE:${PROTO_BINARY_DIR}>")
target_include_directories(livekitclient PUBLIC ${nlohmann_json_SOURCE_DIR}/single_include)

target_link_libraries(livekitclient PUBLIC webrtc)
target_link_libraries(livekitclient PRIVATE
  $<BUILD_INTERFACE:plog::plog>)
target_link_libraries(livekitclient PRIVATE protobuf::libprotobuf)
target_link_libraries(livekitclient PUBLIC livekit_proto)
target_link_libraries(livekitclient PRIVATE nlohmann_json::nlohmann_json)
#target_link_libraries(livekitclient PRIVATE absl::strings absl::string_view)
target_link_libraries(livekitclient PRIVATE ${LIBWEBSOCKETS_LIBRARIES})
target_link_libraries(livekitclient PRIVATE libuv libssl zlib OpenSSL::SSL)

if (WIN32)
  target_link_libraries(livekitclient PRIVATE winmm.lib wmcodecdspuuid.lib dmoguids.lib Iphlpapi.lib Msdmo.lib Secur32.lib Crypt32 Userenv Ws2_32)
endif()

if (WIN32)
  # webrtc
  add_definitions(-DWEBRTC_WIN)
  add_definitions(-DWIN32_LEAN_AND_MEAN)
  if(MSVC)
    add_definitions(-DNOMINMAX)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
  endif()
endif()

if(NOT BUILD_EXAMPLES)
  set(BUILD_EXAMPLES true)
endif()
# examples
if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(NOT BUILD_TEST)
  set(BUILD_TEST true)
endif()
# test
if(BUILD_TEST)
  add_subdirectory(test)
endif()
