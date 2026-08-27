# Windows SDK packaging and deployment

The prebuilt Windows LiveKit Client C++ SDK is a DLL distribution containing matching Release and
Debug binaries. Applications choose the configuration through the imported CMake target:

```cmake
find_package(LiveKitClient CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE LiveKitClient::livekitclient)
```

Release uses `livekitclient.dll`; Debug uses `livekitclientd.dll` and includes its PDB. The DLL's C
ABI in `livekit/capi/livekit.h` is the stable binary boundary. Exported C++ symbols require the same
MSVC toolset and compatible build settings and are not a cross-compiler or cross-release ABI
promise.

## Build the WebRTC prerequisites

A true MSVC Debug DLL cannot link the Release `webrtc.lib`: Release uses `/MT` and
`_ITERATOR_DEBUG_LEVEL=0`, while Debug uses `/MTd` and level 2. Build both H264-enabled packages
from the local `webrtc-build` checkout:

```powershell
Push-Location ..\webrtc-build\build
.\build_windows.cmd release x64 h264
.\build_windows.cmd debug x64 h264
Pop-Location
```

The expected roots are:

```text
../webrtc-build/build/_package/windows_x86_64/release/webrtc
../webrtc-build/build/_package/windows_x86_64/debug/webrtc
```

## Build and package the DLL SDK

Use separate CMake build trees because each configuration links a different WebRTC package. Both
trees use the same Visual Studio toolset, media-capture checkout, and `x64-windows-static` vcpkg
dependencies.

```powershell
$common = @(
  '-G', 'Visual Studio 17 2022', '-A', 'x64',
  '-DBUILD_SHARED_LIBS=ON',
  '-DBUILD_EXAMPLES=OFF', '-DBUILD_TEST=OFF',
  '-DLIBWEBRTC_USE_H264=ON',
  '-DMEDIA_CAPTURE_ROOT=E:/path/to/media-capture',
  '-DCMAKE_TOOLCHAIN_FILE=E:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake',
  '-DVCPKG_TARGET_TRIPLET=x64-windows-static'
)

cmake -S . -B out/build/sdk-dll-release @common `
  -DLIBWEBRTC_ROOT=E:/workspace/cpp/lk-sdk/webrtc-build/build/_package/windows_x86_64/release/webrtc
cmake -S . -B out/build/sdk-dll-debug @common `
  -DLIBWEBRTC_ROOT=E:/workspace/cpp/lk-sdk/webrtc-build/build/_package/windows_x86_64/debug/webrtc

.\cmake\package-windows-dll.ps1 `
  -ReleaseBuildDirectory out/build/sdk-dll-release `
  -DebugBuildDirectory out/build/sdk-dll-debug `
  -OutputDirectory out/package
```

The packaging script builds, installs, validates, and combines both configurations into
`livekit-client-cpp-<version>-Windows-x64-dll.zip`, then prints its SHA256 digest.

## Installed layout

```text
include/livekit/                         public C and C++ headers
lib/Release/livekitclient.lib           Release DLL import library
lib/Debug/livekitclientd.lib            Debug DLL import library
lib/cmake/LiveKitClient/                CMake package config and imported target
bin/Release/livekitclient.dll           Release SDK runtime
bin/Release/websockets.dll              Release WebSocket runtime
bin/Debug/livekitclientd.dll             Debug SDK runtime
bin/Debug/livekitclientd.pdb             Debug symbols
bin/Debug/websockets.dll                 Debug WebSocket runtime
share/livekit-client-cpp/               licenses, versions, and deployment docs
```

Copy the two DLLs from the selected configuration's `bin` directory next to the application, or
add that directory to the process DLL search path. Do not mix Release and Debug directories.

The selected WebRTC package determines codec support. H264 is enabled by default, so both packages
must be built with H264 unless the SDK is explicitly configured with `-DLIBWEBRTC_USE_H264=OFF`.

## Installed-package smoke test

Extract the ZIP, point `CMAKE_PREFIX_PATH` at its top-level directory, then build either
configuration. The same consumer build tree can select the matching imported SDK binary:

```powershell
cmake -S test/consumer -B out/build/package-consumer `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=E:/path/to/extracted/livekit-client-cpp-sdk
cmake --build out/build/package-consumer --config Release
cmake --build out/build/package-consumer --config Debug
```

`test/consumer` builds both a C++ executable and a source file compiled as C. Run
`test/consumer/check-c-exports.ps1` against both DLLs to verify every `LKC_API` declaration.

## Static source build

Static consumers build the SDK from source and provide the configuration-matching WebRTC package:

```powershell
cmake -S . -B out/build/sdk-static <common dependency options> `
  -DBUILD_SHARED_LIBS=OFF `
  -DLIBWEBRTC_ROOT=E:/path/to/configuration-matching/webrtc
cmake --build out/build/sdk-static --config Release --parallel
```

Static consumers must use the same static MSVC runtime and dependency configuration as WebRTC. A
static application still deploys `websockets.dll`, which isolates libwebsockets' mbedTLS symbols
from WebRTC's BoringSSL symbols.
