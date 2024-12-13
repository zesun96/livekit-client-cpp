# ----------------------------------------------------------------------------
#  Root CMake file for livekit-client-cpp-sdk
#
# ----------------------------------------------------------------------------
cmake_minimum_required (VERSION 3.11)

project ("livekit-client-cpp-sdk")

set(PROJECT_DESCRIPTION "C/C++ livekit sdk")

include(GNUInstallDirs)

# C++ standard requirements.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
include(cmake/FindWebRTC.cmake)

set(CMAKE_POLICY_DEFAULT_CMP0048 NEW)
if (NOT TARGET plog)
    add_subdirectory(deps/plog EXCLUDE_FROM_ALL)
endif()
if (NOT TARGET websocket-client)
    add_subdirectory(deps/websocket-client EXCLUDE_FROM_ALL)
endif()

message(STATUS "libwebrtc source dir: ${libwebrtc_SOURCE_DIR}")
message(STATUS "plog source dir: ${plog_SOURCE_DIR}")
message(STATUS "websocket-client source dir: ${websocket-client_SOURCE_DIR}")


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

	src/core/livekit_client.cpp
  src/core/local_participant.cpp
  src/core/peer_transport.cpp
  src/core/remote_participant.cpp
  src/core/room.cpp
  src/core/rtc_engine.cpp

	src/core/detail/signal_client.cpp
  src/core/detail/internals.cpp
)

add_library (livekitclient STATIC

  ${LKC_CORE_SOURCE_FILES}
)

link_directories(${libwebrtc_SOURCE_DIR}/lib)

target_include_directories(livekitclient PUBLIC ${libwebrtc_SOURCE_DIR}/include)
target_include_directories(livekitclient PUBLIC ${libwebrtc_SOURCE_DIR}/include/third_party/abseil-cpp)
target_include_directories(livekitclient PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(livekitclient PRIVATE ${websocket-client_SOURCE_DIR}/include)

target_link_libraries(livekitclient PUBLIC webrtc)
target_link_libraries(livekitclient PRIVATE
	$<BUILD_INTERFACE:plog::plog>)
target_link_libraries(livekitclient PRIVATE
	websocketclient)

if (WIN32)
  # webrtc
  add_definitions(-DWEBRTC_WIN)
  if(MSVC)
	  add_definitions(-DNOMINMAX)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
	endif()
endif()

if(NOT BUILD_EXAMPLES)
	set(BUILD_EXAMPLES true)
endif()
# examples
if(BUILD_EXAMPLES)
	add_subdirectory(examples)
endif()
