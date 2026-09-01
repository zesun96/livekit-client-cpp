# Building from source

This document covers the dependency and compiler constraints for building `livekit-client-cpp`.
For the prebuilt Windows DLL layout and packaging workflow, see
[Windows SDK packaging](WINDOWS_SDK_PACKAGING.md).

## Requirements

The supported Windows build uses:

- Visual Studio 2022 with the x64 C++ toolchain;
- CMake 3.20 or newer;
- C++20;
- vcpkg with a runtime-compatible static-library triplet;
- a Release or Debug libwebrtc package containing `include/` and `lib/`; and
- the `media-capture` source tree, normally checked out next to this repository.

`LKC_MSVC_RUNTIME` selects `dynamic` (the default: `/MD` and `/MDd`) or `static` (`/MT` and
`/MTd`). The SDK, libwebrtc, media-capture, vcpkg dependencies, and native C++ consumer must agree.
Use the included `x64-windows-static-md` overlay triplet when libraries should remain static while
using the dynamic CRT.

## Dependencies

- [livekit-protocol](https://github.com/livekit/protocol): pinned source archive through
  `FetchContent`.
- [libwebrtc](https://github.com/zesun96/webrtc-build): downloaded package by default, or a local
  package selected with `LIBWEBRTC_ROOT`.
- [media-capture](https://github.com/zesun96/media-capture): audio, camera, and screen capture;
  selected with `MEDIA_CAPTURE_ROOT` or `USE_SYSTEM_MEDIA_CAPTURE=ON`.
- [libwebsockets](https://github.com/warmcat/libwebsockets): supplied by vcpkg on Windows.
- [protobuf](https://github.com/protocolbuffers/protobuf): supplied by vcpkg by default.
- [nlohmann/json](https://github.com/nlohmann/json): pinned header-only archive, or
  `USE_SYSTEM_JSON=ON`.
- [plog](https://github.com/SergiusTheBest/plog): pinned archive used by examples, or
  `USE_SYSTEM_PLOG=ON`.

The vcpkg manifest pins protobuf 3.21.12. Newer protobuf releases introduce their own Abseil
dependency, which can conflict with the Abseil headers and ABI embedded in the supported
libwebrtc package. A protobuf upgrade therefore requires complete configure, build, link, and
runtime validation.

## Complete Windows build

Build the matching monolithic libwebrtc package with the `build` branch of
[`zesun96/webrtc-build`](https://github.com/zesun96/webrtc-build):

```powershell
git clone --branch build https://github.com/zesun96/webrtc-build.git
Push-Location webrtc-build\build
.\build_windows.cmd release x64 h264 dynamic # /MD
.\build_windows.cmd debug x64 h264 dynamic   # /MDd
Pop-Location
```

The following example uses local `/MD` Release dependencies and builds examples, unit tests, and
functional tests:

```powershell
cmake -S . -B out/build/vs2022-x64-release `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_OVERLAY_TRIPLETS="$PWD/cmake/triplets" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md `
  -DLIBWEBRTC_ROOT=E:/workspace/cpp/lk-sdk/webrtc-build/build/_package/windows_x86_64/release-md/webrtc `
  -DLIBWEBRTC_USE_H264=ON `
  -DMEDIA_CAPTURE_ROOT=E:/workspace/cpp/lk-sdk/media-capture `
  -DMEDIA_CAPTURE_MINIAUDIO_ROOT=E:/workspace/cpp/lk-sdk/others/miniaudio `
  -DMEDIA_CAPTURE_CAMERA_CAPTURE_ROOT=E:/workspace/cpp/lk-sdk/others/CameraCapture `
  -DMEDIA_CAPTURE_SCREEN_CAPTURE_LITE_ROOT=E:/workspace/cpp/lk-sdk/others/screen_capture_lite `
  -DBUILD_EXAMPLES=ON `
  -DBUILD_TEST=ON `
  -DBUILD_FUNCTIONAL_TESTS=ON `
  -DBUILD_INTEGRATION_TESTS=OFF

cmake --build out/build/vs2022-x64-release --config Release --parallel
ctest --test-dir out/build/vs2022-x64-release -C Release --output-on-failure
```

The three `MEDIA_CAPTURE_*_ROOT` dependency overrides are optional when `media-capture` can obtain
its pinned dependencies normally.

## H264 capability

H264 is enabled by default in this SDK. The selected libwebrtc package must have been built with
the equivalent of:

```text
rtc_use_h264=true
proprietary_codecs=true
ffmpeg_branding="Chrome"
```

The libwebrtc package capability and `LIBWEBRTC_USE_H264` setting must match. Configure with
`-DLIBWEBRTC_USE_H264=OFF` only when consuming a package built without H264. Publishing a requested
codec fails rather than silently negotiating another codec when the linked package lacks it.

## Shared and static builds

Windows builds a DLL by default. The distributable package combines independently built Release
and Debug trees because each configuration must link its matching libwebrtc package. See
[Windows SDK packaging](WINDOWS_SDK_PACKAGING.md) for the exact commands.

A static source build remains available:

```powershell
cmake -S . -B out/build/sdk-static <common-dependency-options> `
  -DBUILD_SHARED_LIBS=OFF `
  -DLKC_MSVC_RUNTIME=static `
  -DLIBWEBRTC_ROOT=C:/path/to/configuration-matching/webrtc
cmake --build out/build/sdk-static --config Release --parallel
```

Static consumers must retain the same MSVC runtime and dependency configuration as libwebrtc. On
Windows, `websockets.dll` remains a runtime dependency even for a static SDK build; this isolates
libwebsockets' mbedTLS symbols from WebRTC's BoringSSL symbols.

## Build options

| Option | Default | Purpose |
| --- | --- | --- |
| `BUILD_SHARED_LIBS` | `ON` on Windows | Build `livekitclient` as a DLL |
| `LKC_MSVC_RUNTIME` | `dynamic` | Select `dynamic` (`/MD[d]`) or `static` (`/MT[d]`) MSVC CRT |
| `LIBWEBRTC_USE_H264` | `ON` | Register H264 support expected in libwebrtc |
| `BUILD_EXAMPLES` | `ON` | Build executable examples |
| `BUILD_TEST` | `ON` | Build unit tests |
| `BUILD_FUNCTIONAL_TESTS` | `ON` | Build local SDK behavior tests |
| `BUILD_INTEGRATION_TESTS` | `OFF` | Build opt-in real-server tests |
| `BUILD_LEGACY_TEST_TOOLS` | `OFF` | Build non-deterministic legacy WebRTC tools |
| `LKC_BUILD_DOCUMENTATION` | `OFF` | Build and install the Doxygen public API reference |
| `LKC_DOCUMENTATION_ONLY` | `OFF` | Configure only documentation, without runtime dependencies |
| `LKC_ENABLE_VLD` | `OFF` | Instrument MSVC Debug tests with Visual Leak Detector |
| `LKC_VLD_ROOT` | empty | Existing Visual Leak Detector installation root |
| `USE_SYSTEM_MEDIA_CAPTURE` | `OFF` | Find an installed media-capture package |
| `USE_SYSTEM_JSON` | `OFF` | Find an installed nlohmann/json package |
| `USE_SYSTEM_PLOG` | `OFF` | Find an installed plog package |

## Installed-package smoke test

`test/consumer` can build against either the source tree or an installed SDK. It compiles one C++
and one pure C executable. See [Windows SDK packaging](WINDOWS_SDK_PACKAGING.md) for the installed
package command and [Integration testing](integration.md) for the latest recorded package result.

## API documentation

The Doxygen reference has a documentation-only configuration mode, so generating API documentation
does not require libwebrtc, vcpkg, or media-capture. See
[Public API reference](API_REFERENCE.md) for build, output, and installation commands.

Formatting, Clang-Tidy, and Visual Leak Detector commands are documented in
[Engineering quality gates](QUALITY_GATES.md).
