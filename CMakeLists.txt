# ----------------------------------------------------------------------------
#  Root CMake file for livekit-client-cpp-sdk
#
# ----------------------------------------------------------------------------
cmake_minimum_required (VERSION 3.11)

project ("livekit-client-cpp-sdk")

# C++ standard requirements.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
include(cmake/FindWebRTC.cmake)

message(STATUS "libwebrtc include: ${libwebrtc_SOURCE_DIR}")
message(STATUS "libwebrtc library: ${libwebrtc_LIBRARY_DIR}")

set(
	LKC_SOURCE_FILES

	src/livekit_client.cpp
)

add_library (livekitclient STATIC
	${LKC_HEADER_FILES}
	${LKC_SOURCE_FILES}
)

link_directories(${libwebrtc_SOURCE_DIR}/lib)

target_include_directories(livekitclient PUBLIC ${libwebrtc_SOURCE_DIR}/include)
target_include_directories(livekitclient PUBLIC ${libwebrtc_SOURCE_DIR}/include/third_party/abseil-cpp)
target_include_directories(livekitclient PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(livekitclient PUBLIC webrtc)

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
