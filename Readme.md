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

- [x] WebSocket signaling and room lifecycle
- [x] Unified connection state change events across the full room lifecycle
- [x] Protocol-level signal resume with SyncState/ICE restart and automatic full-reconnect fallback
- [x] Structured LiveKit disconnect reasons in C++ and C APIs
- [x] Room and participant state, track publications, active speakers, and quality events
- [x] Room metadata and recording status change events
- [x] Participant metadata, display name, and attribute updates
- [x] Participant permission snapshots and change events
- [x] Connection state, identity lookup, local track mute, and remote subscription controls
- [x] Publisher track subscription permissions with reconnect restoration
- [x] Track subscription failure events and retained protocol error details
- [x] Remote video quality/dimensions/FPS preferences and subscription/stream state events
- [x] Stable C ABI with opaque handles and callbacks
- [x] Audio publishing and receiving (signed 16-bit PCM)
- [x] Video publishing and receiving (I420/VP8)
- [x] Reliable and lossy data messages
- [x] SIP DTMF publishing and receiving
- [x] Structured chat messages with stable IDs and edits
- [x] Transcription segment events with language and timing metadata
- [x] Metrics batches with time-series and event samples
- [x] Chunked file transfer over LiveKit data streams
- [x] Incremental text/byte stream writers and topic-scoped chunk handlers
- [x] Bounded DataChannel backpressure with high/low-water events
- [x] Participant RPC with ACK/response timeouts, standard errors, and C API support
- [ ] E2ee

## Dependencies

- [livekit-protocol](https://github.com/livekit/protocol) (pinned source archive via CMake FetchContent)
- [plog](https://github.com/SergiusTheBest/plog) (release archive via CMake FetchContent)
- [libwebsockets](https://github.com/warmcat/libwebsockets)
- [libwebrtc](https://github.com/livekit/rust-sdks/releases) (cmake FetchContent by default)
- [protobuf](https://github.com/protocolbuffers/protobuf) (cmake find_package by default)
- [nlohmann_json](https://github.com/nlohmann/json) (header-only release archive via CMake FetchContent)

The FetchContent dependencies use versioned archives with SHA-256 checksums, so
Git submodules are not required. To use packages installed by a package manager,
configure with `-DUSE_SYSTEM_JSON=ON` and/or `-DUSE_SYSTEM_PLOG=ON`.

The vcpkg manifest pins protobuf 3.21.12 because newer protobuf releases bring
their own Abseil dependency, which can conflict with the Abseil headers and ABI
embedded in the currently supported libwebrtc build.

To consume a locally built libwebrtc package instead of downloading one, point
CMake at a directory containing `include/` and `lib/`. The following command
builds the SDK, examples, and local tests with Visual Studio 2022 and the same
static MSVC runtime (`/MT`) used by the packaged libwebrtc:

```powershell
cmake -S . -B out/build/vs2022-x64-release `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static `
  -DLIBWEBRTC_ROOT=E:/workspace/cpp/lk-sdk/webrtc-build/build/_package/windows_x86_64/release/webrtc `
  -DBUILD_EXAMPLES=ON `
  -DBUILD_TEST=ON `
  -DBUILD_FUNCTIONAL_TESTS=ON `
  -DBUILD_INTEGRATION_TESTS=OFF

cmake --build out/build/vs2022-x64-release --config Release --parallel
ctest --test-dir out/build/vs2022-x64-release -C Release --output-on-failure
```

The resulting static library is
`out/build/vs2022-x64-release/Release/livekitclient.lib`. On Windows,
libwebsockets is intentionally kept in a DLL so its mbedTLS symbols remain
isolated from the BoringSSL symbols embedded in `webrtc.lib`. CMake copies
`websockets.dll` next to SDK executables automatically; applications consuming
the static library must deploy that DLL with their executable.

Video uses libwebrtc's VP8 encoder and decoder template factories. The
libwebrtc package must contain the corresponding libvpx implementation.

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

To run integration tests, configure with `-DBUILD_INTEGRATION_TESTS=ON` and set
the server URL and short-lived tokens in the environment:

- `LIVEKIT_TOKEN_SINGLE`: a unique identity for the single-client lifecycle test.
- `LIVEKIT_TOKEN` and `LIVEKIT_TOKEN_2`: two different identities in the same room for the
  participant, audio, video, data-message, text/byte-stream, and file-transfer tests.
- `LIVEKIT_TOKEN_2_UPDATE` (optional): replaces `LIVEKIT_TOKEN_2` in the participant-state test
  and must be generated with `lk token create --allow-update-metadata`; when omitted,
  metadata/name/attribute update checks are skipped.

Tokens should be generated by a trusted backend or `lk token create`; API secrets do not belong in
the client SDK, examples, test source, or repository configuration.

```powershell
$env:LIVEKIT_URL = "http://<livekit-host>:7880/rtc"
$env:LIVEKIT_TOKEN_SINGLE = "<unique-client-token>"
$env:LIVEKIT_TOKEN = "<first-client-token>"
$env:LIVEKIT_TOKEN_2 = "<second-client-token>"
$env:LIVEKIT_TOKEN_2_UPDATE = "<second-client-token-with-update-permission>" # optional

ctest --test-dir out/build/tests -L integration --output-on-failure
```

The old manual WebRTC test executable is excluded by default because it is not
deterministic; enable it only with `-DBUILD_LEGACY_TEST_TOOLS=ON`.

## Examples

See the [examples guide](examples/README.md) for build commands, arguments, environment variables,
and end-to-end audio, video, data-message, and file-transfer verification.

## C API

Pure C applications include `livekit/capi/livekit.h` and link the same `livekitclient` library.
The API uses opaque handles, caller-owned output buffers, and C function-pointer callbacks; no C++
type or exception crosses the ABI boundary. See the [`c_sample`](examples/c_sample/sample.c) example
for room creation, callback registration, connection, string retrieval, and deterministic cleanup.

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
