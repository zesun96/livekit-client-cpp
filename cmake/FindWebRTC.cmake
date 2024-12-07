# Find WebRTC include path

set(WEBRTC_DOWNLOAD_URL "https://github.com/livekit/rust-sdks/releases/download/webrtc-dac8015-6/webrtc-win-x64-release.zip")

set(WEBRTC_DIR "${CMAKE_SOURCE_DIR}/deps/libwebrtc")

# download libwebrtc
FetchContent_Declare(
    libwebrtc
    URL  ${WEBRTC_DOWNLOAD_URL}
    SOURCE_DIR  ${WEBRTC_DIR}
)

FetchContent_MakeAvailable(libwebrtc)

message(STATUS "libwebrtc source dir: ${libwebrtc_SOURCE_DIR}")

