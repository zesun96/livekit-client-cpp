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

The dynamic CRT is the better fit for a DLL-oriented package because SDK, dependencies, and
consumer share the same CRT allocation boundary. It removes the `/MT` cross-DLL heap/STL mismatch,
but it does not turn the native C++ surface into a stable ABI.

## Build the WebRTC prerequisites

A true MSVC Debug DLL cannot link the Release `webrtc.lib`: Release and Debug use different CRT
libraries and iterator-debug levels. For a DLL-oriented build, build both H264-enabled dynamic-CRT
packages from the `build` branch of
[`zesun96/webrtc-build`](https://github.com/zesun96/webrtc-build):

```powershell
git clone --branch build https://github.com/zesun96/webrtc-build.git
Push-Location webrtc-build\build
.\build_windows.cmd release x64 h264 dynamic # /MD
.\build_windows.cmd debug x64 h264 dynamic   # /MDd
Pop-Location
```

The expected roots are:

```text
../webrtc-build/build/_package/windows_x86_64/release-md/webrtc
../webrtc-build/build/_package/windows_x86_64/debug-mdd/webrtc
```

Omit the final `dynamic` argument to retain the compatibility `/MT` and `/MTd` packages in the
original `release` and `debug` directories. Do not mix the two runtime families.

## Build and package the DLL SDK

Use separate CMake build trees because each configuration links a different WebRTC package. Both
trees use the same Visual Studio toolset, media-capture checkout, and vcpkg dependencies. The
included `x64-windows-static-md` triplet keeps dependency libraries static but compiles them with
the dynamic CRT.

```powershell
$common = @(
  '-G', 'Visual Studio 17 2022', '-A', 'x64',
  '-DBUILD_SHARED_LIBS=ON',
  '-DBUILD_EXAMPLES=OFF', '-DBUILD_TEST=OFF',
  '-DLKC_MSVC_RUNTIME=dynamic',
  '-DLIBWEBRTC_USE_H264=ON',
  '-DMEDIA_CAPTURE_ROOT=E:/path/to/media-capture',
  '-DCMAKE_TOOLCHAIN_FILE=E:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake',
  '-DVCPKG_OVERLAY_TRIPLETS=E:/workspace/cpp/lk-sdk/livekit-client-cpp/cmake/triplets',
  '-DVCPKG_TARGET_TRIPLET=x64-windows-static-md'
)

cmake -S . -B out/build/sdk-dll-release @common `
  -DLIBWEBRTC_ROOT=E:/workspace/cpp/lk-sdk/webrtc-build/build/_package/windows_x86_64/release-md/webrtc
cmake -S . -B out/build/sdk-dll-debug @common `
  -DLIBWEBRTC_ROOT=E:/workspace/cpp/lk-sdk/webrtc-build/build/_package/windows_x86_64/debug-mdd/webrtc

.\cmake\package-windows-dll.ps1 `
  -ReleaseBuildDirectory out/build/sdk-dll-release `
  -DebugBuildDirectory out/build/sdk-dll-debug `
  -OutputDirectory out/package
```

The packaging script builds, installs, validates, and combines both configurations into
`livekit-client-cpp-<version>-windows-x64-v143-md-dll.zip`, then prints its SHA256 digest. The
filename is part of the compatibility contract: platform, architecture, MSVC toolset, CRT, and
library type are all explicit. An explicitly requested static-CRT build uses `mt` instead of `md`.

The installed `LiveKitClientConfig.cmake` exposes `LiveKitClient_ARCH`,
`LiveKitClient_MSVC_TOOLSET`, `LiveKitClient_MSVC_RUNTIME`, and
`LiveKitClient_CPP_ABI_COMPATIBLE`. It rejects a non-x64 target and warns when a native C++
consumer selects a different toolset or CRT. Those warnings do not reject stable C ABI consumers.

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
The `/MDd` Debug DLL requires the Visual Studio 2022 Debug CRT and is for development machines; the
Debug CRT is not a redistributable production dependency.

The selected WebRTC package determines codec support. H264 is enabled by default, so both packages
must be built with H264 unless the SDK is explicitly configured with `-DLIBWEBRTC_USE_H264=OFF`.

## Installed-package smoke test

Extract the ZIP, point `CMAKE_PREFIX_PATH` at its top-level directory, then build either
configuration. The same consumer build tree can select the matching imported SDK binary:

```powershell
cmake -S test/consumer -B out/build/package-consumer `
  -G "Visual Studio 17 2022" -A x64 `
  -DLKC_MSVC_RUNTIME=dynamic `
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
  -DLKC_MSVC_RUNTIME=static `
  -DLIBWEBRTC_ROOT=E:/path/to/configuration-matching/webrtc
cmake --build out/build/sdk-static --config Release --parallel
```

Static consumers must use the same MSVC runtime and dependency configuration as WebRTC. A static
application still deploys `websockets.dll`, which isolates libwebsockets' mbedTLS symbols from
WebRTC's BoringSSL symbols.
