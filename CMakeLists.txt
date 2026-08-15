# ----------------------------------------------------------------------------
#  Root CMake file for livekit-client-cpp-sdk
#
# ----------------------------------------------------------------------------
cmake_minimum_required (VERSION 3.15)

project ("livekit-client-cpp-sdk")

set(PROJECT_DESCRIPTION "C/C++ livekit sdk")

if(MSVC)
  cmake_policy(SET CMP0091 NEW)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(GNUInstallDirs)

# C++ standard requirements.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_POLICY_DEFAULT_CMP0048 NEW)

option(USE_SYSTEM_PROTOBUF "Use system protobuf" ON)
option(USE_SYSTEM_JSON "Use system nlohmann/json" OFF)
option(USE_SYSTEM_ABSL "Use system Abseil" OFF)
option(USE_SYSTEM_PLOG "Use system plog" OFF)
option(BUILD_EXAMPLES "Build examples" ON)
option(BUILD_TEST "Build tests" ON)
option(BUILD_FUNCTIONAL_TESTS "Build local functional tests" ON)
option(BUILD_INTEGRATION_TESTS "Build tests that connect to a LiveKit server" OFF)
option(BUILD_LEGACY_TEST_TOOLS "Build legacy manual WebRTC test programs" OFF)

include(FetchContent)
include(cmake/Dependencies.cmake)
include(cmake/FindWebRTC.cmake)

if(USE_SYSTEM_ABSL)
  find_package(absl CONFIG REQUIRED)
endif()

find_package(libwebsockets CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

if(TARGET websockets_shared)
  set(LKC_LIBWEBSOCKETS_TARGET websockets_shared)
else()
  set(LKC_LIBWEBSOCKETS_TARGET websockets)
endif()

if (NOT WIN32)
  pkg_check_modules(LIBWEBSOCKETS REQUIRED libwebsockets)
endif()

if(USE_SYSTEM_PROTOBUF)
  find_package(Protobuf CONFIG REQUIRED)
else()
  if (NOT TARGET probobuf)
    add_subdirectory(deps/probobuf EXCLUDE_FROM_ALL)
  endif()
endif()

if(Protobuf_PROTOC_EXECUTABLE)
  if(NOT TARGET protobuf::protoc)
    add_executable(protobuf::protoc IMPORTED)
  endif()
  set_property(TARGET protobuf::protoc PROPERTY
    IMPORTED_LOCATION "${Protobuf_PROTOC_EXECUTABLE}")
endif()


message(STATUS "libwebrtc source dir: ${libwebrtc_SOURCE_DIR}")
message(STATUS "plog source dir: ${plog_SOURCE_DIR}")
message(STATUS "libwebsockets include dir: ${LIBWEBSOCKETS_INCLUDE_DIRS}")
message(STATUS "libwebsockets lib: ${LIBWEBSOCKETS_LIBRARIES}")
message(STATUS "protobuf include dir: ${PROTOBUF_INCLUDE_DIRS}")
message(STATUS "protobuf libraries: ${PROTOBUF_LIBRARIES}")
message(STATUS "nlohmann_json source dir: ${nlohmann_json_SOURCE_DIR}")

set(LK_PROTO_PATH "${livekit_protocol_SOURCE_DIR}/protobufs")
set(LK_PROTO_FILES
  ${LK_PROTO_PATH}/livekit_models.proto
  ${LK_PROTO_PATH}/livekit_metrics.proto
  ${LK_PROTO_PATH}/livekit_rtc.proto
  ${LK_PROTO_PATH}/logger/options.proto
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

  src/core/track/audio_source.cpp
  src/core/track/audio_track.cpp
  src/core/track/local_audio_track.cpp
  src/core/track/local_track.cpp
  src/core/track/local_track_publication.cpp
  src/core/track/local_video_track.cpp
  src/core/track/media_stream_track.cpp
  src/core/track/remote_audio_track.cpp
  src/core/track/remote_track.cpp
  src/core/track/remote_track_publication.cpp
  src/core/track/remote_video_track.cpp
  src/core/track/rtc_stats.cpp
  src/core/track/track_factory.cpp
  src/core/track/track.cpp
  src/core/track/track_publication.cpp
  src/core/track/video_track.cpp
  src/core/track/video_source.cpp

  src/core/option/defalut_option.cpp
  src/core/option/reconnect_policy.cpp

  src/core/participant/local_participant.cpp
  src/core/participant/participant.cpp
  src/core/participant/remote_participant.cpp

  src/core/livekit_client.cpp
  src/core/media_device.cpp
  src/core/room.cpp

  src/capi/livekit.cpp

  src/core/detail/audio_device.cpp
  src/core/detail/converted_proto.cpp
  src/core/detail/data_channel_backpressure.cpp
  src/core/detail/data_stream_compression.cpp
  src/core/detail/debouncer.cpp
  src/core/detail/internals.cpp
  src/core/detail/global_task_queue.cpp
  src/core/detail/peer_transport.cpp
  src/core/detail/peer_transport_factory.cpp
  src/core/detail/rtc_engine.cpp
  src/core/detail/rtc_session.cpp
  src/core/detail/signal_client.cpp
  src/core/detail/signal_url.cpp
  src/core/detail/uri.cpp
  src/core/detail/utils.cpp
  src/core/detail/video_encoding.cpp
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

target_include_directories(livekitclient PUBLIC ${libwebrtc_SOURCE_DIR}/include)
target_include_directories(
  livekitclient
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)
target_include_directories(livekitclient PUBLIC ${libwebrtc_SOURCE_DIR}/include/third_party/abseil-cpp)
target_include_directories(livekitclient PRIVATE ${PROTOBUF_INCLUDE_DIRS})
target_include_directories(livekitclient PUBLIC "$<BUILD_INTERFACE:${PROTO_BINARY_DIR}>")

target_link_directories(livekitclient PUBLIC ${libwebrtc_SOURCE_DIR}/lib)
target_link_libraries(livekitclient PUBLIC webrtc)
target_link_libraries(livekitclient PRIVATE
  $<BUILD_INTERFACE:plog::plog>)
target_link_libraries(livekitclient PRIVATE protobuf::libprotobuf)
target_link_libraries(livekitclient PUBLIC livekit_proto)
target_link_libraries(livekitclient PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(livekitclient PRIVATE ZLIB::ZLIB)
#target_link_libraries(livekitclient PRIVATE absl::strings absl::string_view)
target_link_libraries(livekitclient PRIVATE ${LKC_LIBWEBSOCKETS_TARGET} libuv::uv_a)

if (WIN32)
  target_link_libraries(
    livekitclient
    PRIVATE winmm.lib wmcodecdspuuid.lib dmoguids.lib Iphlpapi.lib Msdmo.lib Secur32.lib Crypt32
            Userenv Ws2_32 Bcrypt.lib Ole32.lib Propsys.lib Strmiids.lib
  )
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

# examples
if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# test
if(BUILD_TEST)
  enable_testing()
  add_subdirectory(test)
endif()
