# livekit-client-cpp-sdk

A C++20 client SDK for [LiveKit](https://livekit.io/), with a native C++ API and a stable C ABI.

## Platform status

| Platform | Status |
| --- | --- |
| Microsoft Windows | Supported |
| GNU/Linux | Planned |
| Apple macOS | Planned |
| iOS | Planned |
| Android | Planned |

## Highlights

- Room signaling, participant state, track publication, subscriptions, and lifecycle events.
- Signal resume, ICE restart, full-reconnect fallback, dynamic token sources, room migration, and
  configurable retry policy.
- PCM audio and multi-format video publishing (RGBA, ABGR, ARGB, BGRA, RGB24, I420/I420A,
  I422, I444, I010, and NV12) with VP8, VP9, H264, and AV1 transport.
- Bounded pull-based `AudioStream` and `VideoStream` readers alongside remote-frame callbacks.
- End-to-end video frame metadata with timestamps, frame IDs, and application bytes.
- Native microphone, camera, monitor, window, system-audio capture, and remote-audio playback.
- WebRTC AEC, AGC, and noise suppression for native microphone capture.
- Reliable/lossy data, DataTrack schemas, DataStreams, chat, transcription, metrics, SIP DTMF,
  file transfer, and participant RPC.
- Media and data E2EE with key rotation, ratcheting, reconnect recovery, and C API coverage.
- RTC statistics, bounded DataChannel backpressure, simulcast/dynacast, SVC, and backup codecs.
- A stable C ABI based on opaque handles, explicit ownership, callbacks, and structured errors.
- Application-owned logging sinks with independent LiveKit, WebRTC, and WebSocket levels.
- Optional Perfetto/Chrome Trace compatible lifecycle, signaling, transport, track, data, RPC, and
  E2EE tracing through C++ and C APIs.

See [SDK features](docs/FEATURES.md) for capability boundaries and advanced behavior.

## Quick start

The project currently targets Visual Studio 2022 on Windows. A source build needs CMake, vcpkg, a
configuration-matching libwebrtc package, and the sibling `media-capture` source tree.

```powershell
cmake -S . -B out/build/vs2022-x64-release `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static `
  -DLIBWEBRTC_ROOT=C:/path/to/release/webrtc `
  -DMEDIA_CAPTURE_ROOT=C:/path/to/media-capture `
  -DBUILD_EXAMPLES=ON -DBUILD_TEST=ON -DBUILD_FUNCTIONAL_TESTS=ON

cmake --build out/build/vs2022-x64-release --config Release --parallel
ctest --test-dir out/build/vs2022-x64-release -C Release --output-on-failure
```

H264 is enabled by default, so the selected libwebrtc package must include H264 support. Pass
`-DLIBWEBRTC_USE_H264=OFF` only when intentionally using a package built without H264.

For dependency choices, source builds, static runtime requirements, and a complete local command,
see [Building from source](docs/BUILDING.md).

## Consuming the Windows DLL package

The Windows SDK ZIP contains Release and Debug DLLs, matching import libraries, the Debug PDB,
headers, runtime dependencies, and an imported CMake target:

```cmake
find_package(LiveKitClient CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE LiveKitClient::livekitclient)
```

The target selects `livekitclient.dll` or `livekitclientd.dll` for the active configuration.
Deploy it together with the configuration-matching `websockets.dll`. Static builds remain
available from source but are not included in the prebuilt Windows package.

See [Windows SDK packaging and deployment](docs/WINDOWS_SDK_PACKAGING.md) for packaging,
installation, runtime layout, and installed-package consumer checks.

## Documentation

| Topic | Document |
| --- | --- |
| Dependencies and source builds | [Building from source](docs/BUILDING.md) |
| Capability details and API behavior | [SDK features](docs/FEATURES.md) |
| Video frame metadata | [Frame metadata](docs/FRAME_METADATA.md) / [帧元数据](docs/FRAME_METADATA_zh.md) |
| Integration matrix and recorded acceptance results | [Integration testing](docs/integration.md) |
| Reliability, soak, and weak-network procedures | [Reliability testing](docs/RELIABILITY_TESTING.md) |
| Windows DLL packaging and deployment | [Windows SDK packaging](docs/WINDOWS_SDK_PACKAGING.md) |
| Media devices, playback, and audio processing | [Media device design](docs/design/media-device-design.md) |
| E2EE design and interoperability | [E2EE](docs/E2EE.md) / [E2EE 中文](docs/E2EE_zh.md) |
| Executable examples | [Examples guide](https://github.com/zesun96/livekit-client-cpp/tree/main/examples) |

## Tests

Tests use a checksum-pinned GoogleTest source archive:

- `unit`: deterministic utilities without a LiveKit server and normally without libwebrtc linkage.
- `functional`: local public API, media processing, and lifecycle behavior.
- `integration`: opt-in tests against a real LiveKit server.

```powershell
ctest --test-dir <build-directory> -C Release -L unit --output-on-failure
ctest --test-dir <build-directory> -C Release -L functional --output-on-failure
```

Integration tests require short-lived credentials and may restart a server, alter loopback network
traffic, run for hours, or open physical media devices. Use the documented harness instead of
including them in ordinary test runs. The commands, safety boundaries, and recorded Windows H264
acceptance results are in [Integration testing](docs/integration.md).

## C API

Pure C applications include `livekit/capi/livekit.h` and link the same `livekitclient` target. No
C++ type or exception crosses the ABI boundary. The API provides opaque handles, caller-owned
output buffers, function-pointer callbacks, explicit cleanup, and structured thread-local errors.

See [`c_sample`](https://github.com/zesun96/livekit-client-cpp/blob/main/examples/c_sample/sample.c)
for a complete connection and cleanup flow. The standalone
[`test/consumer/main.c`](https://github.com/zesun96/livekit-client-cpp/blob/main/test/consumer/main.c)
program verifies pure C compilation,
linkage, version reporting, structured errors, runtime initialization, and shutdown against both a
source build and an installed package.

## Dependencies

The main dependencies are LiveKit Protocol, libwebrtc, media-capture, libwebsockets, protobuf,
nlohmann/json, and plog. Downloaded CMake dependencies use pinned archives and SHA-256 checksums;
Git submodules are not required. See [Building from source](docs/BUILDING.md) for version and ABI
constraints.

## Continuous integration

Automatic Windows CI runs are currently paused. `.github/workflows/windows.yml` and
`.github/workflows/windows-integration.yml` can be started manually with `workflow_dispatch` when
their required binary fixtures are configured.

## Thanks

- [LiveKit](https://livekit.io/)
- [webrtc-sdk](https://github.com/webrtc-sdk)
- [protobuf](https://github.com/protocolbuffers/protobuf)
- [nlohmann/json](https://github.com/nlohmann/json)
- [plog](https://github.com/SergiusTheBest/plog)
- [libwebsockets](https://github.com/warmcat/libwebsockets)
- [vcpkg](https://github.com/microsoft/vcpkg)

## License

livekit-client-cpp is licensed under the Apache License 2.0. See [LICENSE](LICENSE).
