# ----------------------------------------------------------------------------
#  Root CMake file for livekit-client-cpp-sdk
#
# ----------------------------------------------------------------------------
cmake_minimum_required (VERSION 3.15)

set(LKC_VERSION 0.4.0)
project ("livekit-client-cpp-sdk" VERSION ${LKC_VERSION} LANGUAGES C CXX)

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
option(USE_SYSTEM_PLOG "Use system plog for examples" OFF)
option(USE_SYSTEM_MEDIA_CAPTURE "Use an installed media-capture package" OFF)
set(MEDIA_CAPTURE_ROOT "${PROJECT_SOURCE_DIR}/../media-capture" CACHE PATH
  "Path to the media-capture source tree")
option(BUILD_EXAMPLES "Build examples" ON)
option(BUILD_TEST "Build tests" ON)
option(BUILD_FUNCTIONAL_TESTS "Build local functional tests" ON)
option(BUILD_INTEGRATION_TESTS "Build tests that connect to a LiveKit server" OFF)
option(BUILD_LEGACY_TEST_TOOLS "Build legacy manual WebRTC test programs" OFF)
option(LIBWEBRTC_USE_H264 "Enable H264 adapters from an H264-enabled libwebrtc package" ON)
if(WIN32)
  set(LKC_BUILD_SHARED_DEFAULT ON)
else()
  set(LKC_BUILD_SHARED_DEFAULT OFF)
endif()
option(BUILD_SHARED_LIBS "Build livekitclient as a shared library" ${LKC_BUILD_SHARED_DEFAULT})
set(OFFICIAL_LIVEKIT_CPP_ROOT "" CACHE PATH
    "Optional extracted official LiveKit C++ SDK root for interoperability tests")

include(FetchContent)
include(cmake/Dependencies.cmake)
include(cmake/FindWebRTC.cmake)

if(USE_SYSTEM_MEDIA_CAPTURE)
  find_package(media-capture CONFIG REQUIRED)
elseif(EXISTS "${MEDIA_CAPTURE_ROOT}/CMakeLists.txt")
  set(MEDIA_CAPTURE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(MEDIA_CAPTURE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(MEDIA_CAPTURE_BUILD_CAMERA ON CACHE BOOL "" FORCE)
  set(MEDIA_CAPTURE_BUILD_SCREEN ON CACHE BOOL "" FORCE)
  add_subdirectory("${MEDIA_CAPTURE_ROOT}"
    "${CMAKE_CURRENT_BINARY_DIR}/_deps/media-capture-build")
else()
  message(FATAL_ERROR
    "media-capture was not found. Set MEDIA_CAPTURE_ROOT or USE_SYSTEM_MEDIA_CAPTURE=ON.")
endif()
if(NOT TARGET media_capture::screen)
  message(FATAL_ERROR "media-capture must provide the media_capture::screen component")
endif()
if(NOT TARGET media_capture::camera)
  message(FATAL_ERROR "media-capture must provide the media_capture::camera component")
endif()

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
if(BUILD_EXAMPLES)
  message(STATUS "plog example source dir: ${plog_SOURCE_DIR}")
endif()
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
configure_file(
  cmake/version.h.in
  "${PROTO_BINARY_DIR}/livekit/core/version.h"
  @ONLY
)

# livekit-proto
add_library(livekit_proto OBJECT ${LK_PROTO_FILES})
set_target_properties(livekit_proto PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(livekit_proto PUBLIC "$<BUILD_INTERFACE:${PROTO_BINARY_DIR}>")
target_include_directories(livekit_proto PUBLIC ${Protobuf_INCLUDE_DIRS})
target_link_libraries(livekit_proto PUBLIC protobuf::libprotobuf)

set(
  LKC_CORE_SOURCE_FILES

  src/core/e2ee/e2ee_manager.cpp
  src/core/e2ee/frame_crypto_transformer.cpp
  src/core/e2ee/key_provider.cpp

  src/capture/audio_capture_adapter.cpp
  src/capture/audio_gain.cpp
  src/capture/camera_capture_adapter.cpp
  src/capture/frame_queue.cpp
  src/capture/webrtc_audio_processor.cpp
  src/capture/screen_capture_adapter.cpp
  src/capture/video_frame_converter.cpp

  src/core/track/audio_source.cpp
  src/core/track/audio_track.cpp
  src/core/track/camera_video_source.cpp
  src/core/track/local_audio_track.cpp
  src/core/track/local_track.cpp
  src/core/track/local_track_publication.cpp
  src/core/track/local_video_track.cpp
  src/core/track/media_stream_track.cpp
  src/core/track/microphone_audio_source.cpp
  src/core/track/screen_video_source.cpp
  src/core/track/system_audio_source.cpp
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

  src/core/data_track.cpp
  src/core/livekit_client.cpp
  src/core/media_device.cpp
  src/core/room.cpp
  src/core/token_source.cpp

  src/capi/livekit.cpp

  src/core/detail/audio_device.cpp
  src/core/detail/converted_proto.cpp
  src/core/detail/data_channel_backpressure.cpp
  src/core/detail/data_track_packet.cpp
  src/core/detail/data_track_proto.cpp
  src/core/detail/data_stream_compression.cpp
  src/core/detail/debouncer.cpp
  src/core/detail/internals.cpp
  src/core/detail/logging.cpp
  src/core/detail/global_task_queue.cpp
  src/core/detail/peer_transport.cpp
  src/core/detail/peer_transport_factory.cpp
  src/core/detail/public_video_frame_converter.cpp
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

add_library (livekitclient

  ${LKC_CORE_SOURCE_FILES}

  ${LK_PROTO_FILES}

  $<TARGET_OBJECTS:livekit_proto>
)
add_library(LiveKitClient::livekitclient ALIAS livekitclient)
target_compile_features(livekitclient PUBLIC cxx_std_20)

protobuf_generate(
    TARGET livekit_proto
    PROTOS ${LK_PROTO_FILES} 
    APPEND_PATH ${LK_PROTO_PATH}
    IMPORT_DIRS ${LK_PROTO_PATH}
    PROTOC_OUT_DIR ${PROTO_BINARY_DIR}
)

target_include_directories(
  livekitclient
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${libwebrtc_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${libwebrtc_SOURCE_DIR}/include/third_party/abseil-cpp>
    $<BUILD_INTERFACE:${PROTO_BINARY_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(livekitclient PRIVATE
  ${PROJECT_SOURCE_DIR}/src/core/detail
  ${libwebrtc_SOURCE_DIR}/include/third_party/boringssl/src/include
  ${libwebrtc_SOURCE_DIR}/include/third_party/libyuv/include
)
target_include_directories(livekitclient PRIVATE ${PROTOBUF_INCLUDE_DIRS})

target_link_directories(livekitclient PRIVATE ${libwebrtc_SOURCE_DIR}/lib)
target_link_libraries(livekitclient PRIVATE webrtc)
target_link_libraries(livekitclient PRIVATE protobuf::libprotobuf)
target_link_libraries(livekitclient PRIVATE $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>)
target_link_libraries(livekitclient PRIVATE ZLIB::ZLIB)
target_link_libraries(livekitclient PRIVATE
  $<BUILD_INTERFACE:media_capture::audio>
  $<BUILD_INTERFACE:media_capture::camera>
  $<BUILD_INTERFACE:media_capture::screen>
  $<INSTALL_INTERFACE:media_capture::audio>
  $<INSTALL_INTERFACE:media_capture::camera>
  $<INSTALL_INTERFACE:media_capture::screen>)
#target_link_libraries(livekitclient PRIVATE absl::strings absl::string_view)
target_link_libraries(livekitclient PRIVATE ${LKC_LIBWEBSOCKETS_TARGET} libuv::uv_a)

if(LIBWEBRTC_USE_H264)
  target_compile_definitions(livekitclient PUBLIC WEBRTC_USE_H264)
endif()

if(BUILD_SHARED_LIBS)
  target_compile_definitions(livekitclient PUBLIC LKC_SHARED)
  target_compile_definitions(livekitclient PRIVATE LKC_BUILDING_LIBRARY)
  set_target_properties(livekitclient PROPERTIES
    DEBUG_POSTFIX d
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    WINDOWS_EXPORT_ALL_SYMBOLS ON
  )
else()
  target_link_directories(livekitclient INTERFACE
    $<BUILD_INTERFACE:${libwebrtc_SOURCE_DIR}/lib>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_LIBDIR}>)
endif()

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

include(CMakePackageConfigHelpers)

if(WIN32)
  set(LKC_INSTALL_BINDIR "${CMAKE_INSTALL_BINDIR}/$<CONFIG>")
  set(LKC_INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}/$<CONFIG>")
else()
  set(LKC_INSTALL_BINDIR "${CMAKE_INSTALL_BINDIR}")
  set(LKC_INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
endif()

install(TARGETS livekitclient
  EXPORT LiveKitClientTargets
  ARCHIVE DESTINATION "${LKC_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${LKC_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${LKC_INSTALL_BINDIR}"
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
if(MSVC AND BUILD_SHARED_LIBS)
  install(FILES "$<TARGET_PDB_FILE:livekitclient>"
    DESTINATION "${LKC_INSTALL_BINDIR}"
    OPTIONAL)
endif()
install(DIRECTORY include/livekit DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE Readme.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/livekit-client-cpp)
install(DIRECTORY docs/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/livekit-client-cpp/docs
  FILES_MATCHING PATTERN "*.md")
if(EXISTS "${libwebrtc_SOURCE_DIR}/NOTICE")
  install(FILES "${libwebrtc_SOURCE_DIR}/NOTICE"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/livekit-client-cpp
    RENAME WEBRTC_NOTICE)
endif()
if(EXISTS "${libwebrtc_SOURCE_DIR}/VERSIONS")
  install(FILES "${libwebrtc_SOURCE_DIR}/VERSIONS"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/livekit-client-cpp
    RENAME WEBRTC_VERSIONS)
endif()

if(WIN32 AND NOT BUILD_SHARED_LIBS)
  install(FILES "${libwebrtc_SOURCE_DIR}/lib/webrtc.lib"
    DESTINATION "${LKC_INSTALL_LIBDIR}")
endif()
if(WIN32 AND TARGET websockets_shared)
  install(FILES "$<TARGET_FILE:websockets_shared>"
    DESTINATION "${LKC_INSTALL_BINDIR}")
endif()

install(EXPORT LiveKitClientTargets
  FILE LiveKitClientTargets.cmake
  NAMESPACE LiveKitClient::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LiveKitClient
)
configure_package_config_file(
  cmake/LiveKitClientConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/LiveKitClientConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LiveKitClient
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/LiveKitClientConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/LiveKitClientConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/LiveKitClientConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LiveKitClient
)

set(CPACK_PACKAGE_NAME "livekit-client-cpp")
set(CPACK_PACKAGE_VENDOR "LiveKit Client C++ contributors")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
if(BUILD_SHARED_LIBS)
  set(LKC_PACKAGE_VARIANT shared)
else()
  set(LKC_PACKAGE_VARIANT static)
endif()
set(CPACK_PACKAGE_FILE_NAME
  "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}-${LKC_PACKAGE_VARIANT}")
if(WIN32)
  set(CPACK_GENERATOR ZIP)
endif()
include(CPack)
