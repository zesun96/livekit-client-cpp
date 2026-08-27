# Windows SDK packaging and deployment

LiveKit Client C++ 0.1.0 can be installed as either a static SDK or a DLL SDK. Both variants expose
the same CMake package and target:

```cmake
find_package(LiveKitClient CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE LiveKitClient::livekitclient)
```

The DLL's C ABI in `livekit/capi/livekit.h` is the stable binary boundary. The Windows build also
exports C++ symbols so a consumer built with the same MSVC toolset and runtime can use the C++ API,
but C++ ABI compatibility across compilers, toolset versions, build modes, or SDK releases is not
promised. Prefer the C ABI when the DLL and application have independent release lifecycles.

## Build and install

Use the same x64 libwebrtc package, media-capture checkout, and vcpkg static triplet described in
the main README. `BUILD_SHARED_LIBS` selects the artifact type; it defaults to `OFF`.

```powershell
# Static SDK
cmake -S . -B out/build/sdk-static <common dependency options> `
  -DBUILD_SHARED_LIBS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TEST=OFF
cmake --build out/build/sdk-static --config Release --parallel
cmake --install out/build/sdk-static --config Release `
  --prefix out/install/livekit-client-cpp-static

# DLL SDK
cmake -S . -B out/build/sdk-shared <common dependency options> `
  -DBUILD_SHARED_LIBS=ON -DBUILD_EXAMPLES=OFF -DBUILD_TEST=OFF
cmake --build out/build/sdk-shared --config Release --parallel
cmake --install out/build/sdk-shared --config Release `
  --prefix out/install/livekit-client-cpp-shared
```

The shared build adds `LKC_SHARED` to the imported target automatically. Applications must not set
`LKC_BUILDING_LIBRARY`; that definition is private to the DLL build.

Generate versioned ZIP archives with CPack:

```powershell
cmake --build out/build/sdk-static --config Release --target package
cmake --build out/build/sdk-shared --config Release --target package
```

The archive names include the SDK version, platform, processor, and `static` or `shared` variant.

## Installed layout

```text
include/livekit/                         public C and C++ headers
lib/livekitclient.lib                   static library or DLL import library
lib/webrtc.lib                          static SDK's pinned WebRTC library
lib/cmake/LiveKitClient/                CMake package config and imported target
lib/cmake/media-capture/                bundled media-capture package metadata
bin/livekitclient.dll                   shared SDK only
bin/websockets.dll                      libwebsockets runtime
share/livekit-client-cpp/               licenses, dependency versions, and deployment docs
```

The static package keeps WebRTC and media-capture in the SDK prefix. Its remaining link-time
dependencies (`protobuf`, `libwebsockets`, `libuv`, and `zlib`) are resolved with
`find_dependency`; configure consumers with the same `x64-windows-static` vcpkg triplet and `/MT`
runtime. The DLL package hides those link-time dependencies from consumers.

## Runtime deployment

For the DLL SDK, copy `bin/livekitclient.dll` and `bin/websockets.dll` next to the application
executable, or add the SDK `bin` directory to the process DLL search path. A static SDK application
still needs `websockets.dll`, because libwebsockets is intentionally isolated from WebRTC's
BoringSSL symbols.

The selected libwebrtc package determines codec support. This repository enables H264 by default,
so release packages must use an H264-enabled libwebrtc build unless configured with
`-DLIBWEBRTC_USE_H264=OFF`.

## Installed-package smoke test

`test/consumer` uses the installed package whenever `LIVEKIT_CLIENT_CPP_SOURCE_DIR` is omitted:

```powershell
cmake -S test/consumer -B out/build/package-consumer `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=E:/path/to/livekit-client-cpp-install
cmake --build out/build/package-consumer --config Release
```

It builds a C++ program and a source file compiled as C. For the shared build, run
`test/consumer/check-c-exports.ps1` to verify that every `LKC_API` declaration is present in the DLL
export table.
