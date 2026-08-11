# livekit-client-cpp-sdk

C++ client SDK for LiveKit in C++20.

Because webrtc requires C++20.

## Support Platforms

- [x] Microsoft Windows
- [ ] GNU/Linux
- [ ] Apple macOS
- [ ] iOS
- [ ] Android

## Features

- [ ] Client Websocket Signal
- [ ] Receiving tracks
- [ ] Publishing tracks
- [ ] Data channels
- [ ] E2ee

## Dependencies

- [livekit-protocol](https://github.com/livekit/protocol) (pinned source archive via CMake FetchContent)
- [plog](https://github.com/SergiusTheBest/plog) (release archive via CMake FetchContent)
- [libwebsockets](https://github.com/warmcat/libwebsockets)
- [libwebrtc](https://github.com/livekit/rust-sdks/releases) (cmake FetchContent by default)
- [protobuf](https://github.com/protocolbuffers/protobuf) (cmake find_package by default)
- [nlohmann_json](https://github.com/nlohmann/json) (header-only release archive via CMake FetchContent)
- [dr_libs](https://github.com/mackron/dr_libs) (pinned source archive via CMake FetchContent, examples only)

The FetchContent dependencies use versioned archives with SHA-256 checksums, so
Git submodules are not required. To use packages installed by a package manager,
configure with `-DUSE_SYSTEM_JSON=ON` and/or `-DUSE_SYSTEM_PLOG=ON`.

The vcpkg manifest pins protobuf 3.21.12 because newer protobuf releases bring
their own Abseil dependency, which can conflict with the Abseil headers and ABI
embedded in the currently supported libwebrtc build.

To consume a locally built libwebrtc package instead of downloading one, point
CMake at a directory containing `include/` and `lib/`:

```powershell
cmake -S . -B out/build/x64-release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DLIBWEBRTC_ROOT=E:/path/to/libwebrtc/win-x64-release
cmake --build out/build/x64-release --config Release
```

## Tests

Tests use GoogleTest 1.15.2 from a small, checksum-verified source archive.
GoogleMock is available to new tests through `GTest::gmock`.

- `unit`: isolated core utilities; no libwebrtc library or LiveKit server required.
- `functional`: local public API and lifecycle behavior; links `livekitclient`.
- `integration`: real server behavior; disabled by default and requires credentials.

```powershell
cmake -S . -B out/build/tests -G Ninja `
  -DBUILD_TEST=ON `
  -DBUILD_FUNCTIONAL_TESTS=ON `
  -DBUILD_INTEGRATION_TESTS=OFF
cmake --build out/build/tests

ctest --test-dir out/build/tests -L unit --output-on-failure
ctest --test-dir out/build/tests -L functional --output-on-failure
```

To run integration tests, configure with `-DBUILD_INTEGRATION_TESTS=ON`, set
`LIVEKIT_URL` and `LIVEKIT_TOKEN` in the environment, then run:

```powershell
ctest --test-dir out/build/tests -L integration --output-on-failure
```

The old manual WebRTC test executable is excluded by default because it is not
deterministic; enable it only with `-DBUILD_LEGACY_TEST_TOOLS=ON`.

## Examples

See [examples](./examples/)

## Thanks

- [livekit](https://livekit.io/)
- [webrtc-sdk](https://github.com/webrtc-sdk)
- [protobuf](https://github.com/protocolbuffers/protobuf)
- [nlohmann_json](https://github.com/nlohmann/json)
- [plog](https://github.com/SergiusTheBest/plog)
- [libwebsockets](https://github.com/warmcat/libwebsockets)
- [vcpkg](https://github.com/livekit/protocol)
- [Azure](https://github.com/Azure)

## License

livekit-client-cpp is licensed under Apache License v2.0.
