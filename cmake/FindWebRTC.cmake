# Find WebRTC include path

set(LIBWEBRTC_ROOT "" CACHE PATH "Path to a prebuilt libwebrtc package containing include/ and lib/")

set(WEBRTC_HOST_URL "https://github.com/zesun96/libwebrtc/releases/download/webrtc-dac8015-4")


if (CMAKE_SYSTEM_NAME MATCHES "Windows")
  set(WEBRTC_PLATFORM "win")
elseif(CMAKE_SYSTEM_NAME MATCHES "Linux")
  set(WEBRTC_PLATFORM "linux")
else()
  set(WEBRTC_PLATFORM "Unknown")
endif()

## Detect TARGET_ARCH ##
if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "i386|i686|amd64|x86_64|AMD64")
	if (CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(WEBRTC_ARCH "x64")
	else(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(WEBRTC_ARCH "x86")
	endif()
elseif (${CMAKE_SYSTEM_PROCESSOR} MATCHES "^aarch64")
	set(WEBRTC_ARCH "arm64")
elseif (${CMAKE_SYSTEM_PROCESSOR} MATCHES "^arm")
	set(WEBRTC_ARCH "arm32")
else()
	message(FATAL_ERROR "Not a valid processor" ${CMAKE_SYSTEM_PROCESSOR})
endif()
  
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(WEBRTC_BUILD_TYPE "debug")
else()
  set(WEBRTC_BUILD_TYPE "release")
endif()

if(LIBWEBRTC_ROOT)
  get_filename_component(LIBWEBRTC_ROOT "${LIBWEBRTC_ROOT}" ABSOLUTE)
  if(NOT EXISTS "${LIBWEBRTC_ROOT}/include" OR NOT EXISTS "${LIBWEBRTC_ROOT}/lib")
    message(FATAL_ERROR "LIBWEBRTC_ROOT must contain include/ and lib/: ${LIBWEBRTC_ROOT}")
  endif()
  FetchContent_Declare(libwebrtc SOURCE_DIR "${LIBWEBRTC_ROOT}")
else()
  set(WEBRTC_DOWNLOAD_URL "${WEBRTC_HOST_URL}/webrtc-${WEBRTC_PLATFORM}-${WEBRTC_ARCH}-${WEBRTC_BUILD_TYPE}.zip")
  set(WEBRTC_DIR "${PROJECT_SOURCE_DIR}/deps/libwebrtc/${WEBRTC_PLATFORM}_${WEBRTC_ARCH}_${WEBRTC_BUILD_TYPE}")
  message(STATUS "WEBRTC_DOWNLOAD_URL: ${WEBRTC_DOWNLOAD_URL}")

  FetchContent_Declare(
    libwebrtc
    URL ${WEBRTC_DOWNLOAD_URL}
    SOURCE_DIR ${WEBRTC_DIR}
  )
endif()

FetchContent_MakeAvailable(libwebrtc)

message(STATUS "libwebrtc source dir: ${libwebrtc_SOURCE_DIR}")
