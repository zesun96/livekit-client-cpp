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
- [x] Configurable reconnect timeout/backoff policy with bounded, interruptible retries
- [x] Structured LiveKit disconnect reasons in C++ and C APIs
- [x] Room and participant state, track publications, active speakers, and quality events
- [x] Room metadata and recording status change events
- [x] Participant metadata, display name, and attribute updates
- [x] Participant permission snapshots and change events
- [x] Local track first-subscriber events
- [x] Connection state, identity lookup, local track mute, and remote subscription controls
- [x] Publisher track subscription permissions with reconnect restoration
- [x] Track subscription failure events and retained protocol error details
- [x] Remote video quality/dimensions/FPS preferences and subscription/stream state events
- [x] Stable C ABI with opaque handles and callbacks
- [x] Audio publishing and receiving (signed 16-bit PCM)
- [x] Video publishing and receiving (I420 with VP8, VP9, H264, or AV1)
- [x] Reliable and lossy data messages
- [x] Typed DataTrack publishing, schema storage/lookup, pull subscriptions, fragmentation, and E2EE
- [x] SIP DTMF publishing and receiving
- [x] Structured chat messages with stable IDs and edits
- [x] Transcription segment events with language and timing metadata
- [x] Metrics batches with time-series and event samples
- [x] Chunked file transfer over LiveKit data streams
- [x] Incremental text/byte stream writers and topic-scoped chunk handlers
- [x] Bounded DataChannel backpressure with high/low-water events
- [x] Participant RPC with ACK/response timeouts, standard errors, and C API support
- [x] Pure C++ E2EE for media and data, with C++/C APIs, key rotation, and reconnect recovery

## Dependencies

- [livekit-protocol](https://github.com/livekit/protocol) (pinned source archive via CMake FetchContent)
- [plog](https://github.com/SergiusTheBest/plog) (release archive via CMake FetchContent)
- [libwebsockets](https://github.com/warmcat/libwebsockets)
- [libwebrtc](https://github.com/zesun96/webrtc-build) (cmake FetchContent by default)
- [media-capture](https://github.com/zesun96/media-capture) (audio, camera, and screen capture
  abstraction; sibling source tree by default)
- [protobuf](https://github.com/protocolbuffers/protobuf) (cmake find_package by default)
- [nlohmann_json](https://github.com/nlohmann/json) (header-only release archive via CMake FetchContent)

FetchContent dependencies use version-pinned release sources, with SHA-256 checksums for archive
downloads, so Git submodules are not required. Set `MEDIA_CAPTURE_ROOT` when `media-capture` is not
checked out next to this repository, or configure with `-DUSE_SYSTEM_MEDIA_CAPTURE=ON` for an
installed package. Package manager builds can also use `-DUSE_SYSTEM_JSON=ON` and/or
`-DUSE_SYSTEM_PLOG=ON`.

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

Windows builds a DLL by default. The distributable SDK ZIP contains both
`livekitclient.dll` (Release) and `livekitclientd.dll` plus its PDB (Debug), with matching import
libraries and runtime dependencies in per-configuration directories. Its imported CMake target
automatically defines `LKC_SHARED` and selects the correct binary for the consumer configuration.
The DLL's explicitly exported C API is the stable ABI. Exported C++ symbols are supported for
same-toolset consumers but are not a cross-compiler or cross-release ABI promise.

Static builds remain available from source with `-DBUILD_SHARED_LIBS=OFF`, but are not part of the
prebuilt Windows SDK distribution. `test/consumer` can either use the source tree or an installed
package and builds both C++ and pure C smoke executables. See
[Windows SDK packaging and deployment](docs/WINDOWS_SDK_PACKAGING.md) for Release/Debug WebRTC,
DLL packaging, static source builds, consumption, and runtime layout commands.

On Windows, libwebsockets is intentionally kept in a DLL so its mbedTLS symbols remain isolated
from the BoringSSL symbols embedded in `webrtc.lib`. Deploy the configuration-matching
`websockets.dll` and `livekitclient[d].dll` next to a DLL consumer. Static consumers must also use
the static MSVC runtime
(`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>`) to match packaged libwebrtc.

Video registers libwebrtc's VP8, VP9, optional OpenH264, and AV1/Dav1d codec adapters. The selected
`TrackPublishOptions::video_codec` (or `lk_track_publish_options_t::video_codec` in C) is applied as
the transceiver codec preference; publishing fails instead of silently negotiating another codec
when the linked libwebrtc package lacks it. To select H264, build libwebrtc with
`rtc_use_h264=true`, `proprietary_codecs=true`, and `ffmpeg_branding="Chrome"`, then configure this
project with `-DLIBWEBRTC_USE_H264=ON`. The package capability and consumer option must match.
H264 is enabled by default; pass `-DLIBWEBRTC_USE_H264=OFF` only when consuming a libwebrtc package
built without H264.
Video publishing supports LiveKit-compatible `q`/`h`/`f` simulcast layers and optional dynacast
layer activation through `RoomOptions::dynacast` for VP8 and H264. VP9 and AV1 use a single RTP
encoding with SVC; `TrackPublishOptions::scalability_mode` selects the mode and defaults to
`L3T3_KEY`. The C API exposes the same setting as `lk_track_publish_options_t::scalability_mode`.
Advanced-codec publications can opt into an H264 or VP8 fallback with
`TrackPublishOptions::backup_video_codec`. The initial publication advertises the fallback and the
SDK creates its independent sender only after LiveKit requests that codec. Select
`BackupCodecPolicy::PreferRegression` (default), `Simulcast`, or `Regression` to control server
selection; `backup_video_encoding` optionally overrides the fallback bitrate and frame rate. The C
API appends equivalent `backup_video_codec_enabled`, `backup_video_codec`, and
`backup_codec_policy` fields, plus `video_encoding` and `backup_video_encoding` settings. After a
video track is published, `LocalParticipantInterface::UpdateVideoEncoding()` can change the maximum
bitrate and frame rate on its existing primary or backup sender without replacing the track or
renegotiating. Passing zero values restores the resolution-derived defaults; backup settings are
retained until the server requests that sender. The C equivalent is
`lk_local_video_track_update_encoding()`. Backup senders currently remain disabled for E2EE
publications, matching the official client capability boundary.

Applications can create native microphone, camera, monitor, and window sources through the C++ or
C device APIs, then publish them with the normal local-track helpers. Native system-audio capture
uses the selected output endpoint on Windows, and connected rooms play WebRTC's mixed remote audio
through a selectable output device. Applications that already capture I420/PCM frames can continue
to use the external source APIs. `PublishScreenShareVideoTrack` and
`PublishScreenShareAudioTrack` set the LiveKit track source. Microphone AEC/AGC/NS processing
counters are available through `MicrophoneAudioSourceInterface::ProcessingStats()` and
`lk_audio_source_microphone_processing_stats`. See the
[media device design](docs/design/media-device-design.md) for device identity, lifecycle,
threading, playback/AEC, system-audio, and validation details.

Published local tracks and subscribed remote tracks expose the selector-scoped libwebrtc
`RTCStatsReport` as JSON through `TrackInterface::GetRTCStats()`. The C API provides the same data
for local track handles through `lk_local_track_rtc_stats`. C++ applications can use
`GetRTCStatsSnapshot()` for normalized RTP stream counters or caller-scheduled `RTCStatsMonitor`
samples for bitrate, RTT, loss, jitter, audio, video, and codec metrics.

Text, byte, file, and incremental DataStreams support optional LiveKit-compatible raw-deflate
compression through the `compress` send option. Compression is disabled by default for backward
compatibility. Compressed streams retain the original byte count in `total_size`; receivers enforce
a 64 MiB compressed-input and decompressed-output limit before dispatching data to applications.

Connection recovery uses `RoomConnectOptions::reconnect_timeout` as the per-attempt RTC upper bound
and invokes `RoomConnectOptions::reconnect_policy` before each full-reconnect attempt. Custom
policies return a delay or `std::nullopt` to stop recovery; `join_retries` remains the maximum number
of full-reconnect attempts. Policy callbacks run on the SDK recovery thread and should return
quickly. The default policy retries immediately and then uses quadratic backoff capped at 7 seconds.

DataTrack schema definitions are stored once by the publishing participant, then referenced by
schema ID when a track is published. Remote participants query the definition by publisher identity;
successful lookups are cached across connection recovery. LiveKit Server must enable
`enable_participant_data_blob` for schema storage and lookup. See the
[`data_track_schema`](examples/data_track_schema/data_track_schema.cpp) example for the complete
store, publish, and frame-send flow.

The stable C ABI exposes the same schema store/query, typed publication, fragmentation, bounded
pull-reader, and room-event flow through the `lk_*_data_track_*` APIs. Local track, reader, pulled
frame, and queried schema handles are caller-owned. DataTrack operations return
`lk_data_track_error_code_t`; `lk_last_error()` supplies the accompanying diagnostic text.

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
  participant, audio, video, E2EE, data-message, text/byte-stream, and file-transfer tests.
- `LIVEKIT_TOKEN_2_UPDATE` (optional): replaces `LIVEKIT_TOKEN_2` in the participant-state test
  and must be generated with `lk token create --allow-update-metadata`; when omitted,
  metadata/name/attribute update checks are skipped.
- `LIVEKIT_HARDWARE_MEDIA=1` (optional): enables real microphone, camera, and monitor capture in
  `PublishesAndReceivesHardwareCapturedMedia`; the test also validates AEC render-reference
  processing and RTP send/receive counters.
- `LIVEKIT_HARDWARE_WINDOW_SOURCE_ID` (optional): replaces the monitor with a specified native
  window in the hardware test. Run the test once without it and once with it for the full desktop
  matrix. Obtain the opaque ID from `EnumerateScreenCaptureSources()` or the C screen-source list API.

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

Destructive recovery checks are kept out of the regular integration suite. The Windows harness
starts an owned server, coordinates an explicit restart, verifies server-issued token refresh
across resume and full reconnect, and restores an existing server in a `finally` block:

```powershell
$apiSecret = Read-Host "LiveKit API secret"
.\test\integration\run_reconnect_matrix.ps1 `
  -ServerExecutable "C:\path\to\livekit-server.exe" `
  -LkExecutable "C:\path\to\lk.exe" `
  -ApiKey "devkey" -ApiSecret $apiSecret `
  -BuildDirectory "out\build\tests" `
  -NodeIp "192.168.1.20" -Port 7880 `
  -ConfigPath "C:\path\to\livekit.yaml" `
  -ReplaceExistingServer
```

Run this command from an elevated PowerShell when the existing server is elevated or Windows
Firewall requires a rule for a newly downloaded server executable. To test a different server
version while restoring the current one, also pass `-ExistingServerExecutable` and, when their
RTC addresses differ, `-ExistingServerNodeIp`. Use `-Scenario Participants`, `-Scenario Restart`,
`-Scenario TokenRefresh`, `-Scenario Media`, `-Scenario E2EE`, `-Scenario CAPI`,
`-Scenario DataRecovery`, `-Scenario CodecMatrix`, `-Scenario Soak`, or `-Scenario WeakNetwork` to
run one part
of the matrix. The participant scenario concurrently joins and leaves four clients and verifies
duplicate-identity replacement and same-identity rejoin. `-Iterations N` repeats the participant,
restart, token-refresh, and media-recovery scenario blocks; the latter covers signal resume, ICE
restart, forced full reconnect, and track/data recovery. The C API scenario restarts the server
with two C ABI rooms, verifies reconnect callbacks and identities, then transfers reliable data
after recovery. See
[Reliability and weak-network testing](docs/RELIABILITY_TESTING.md) for the staged matrix and
acceptance rules.
The data-recovery scenario verifies DataStream and RPC before and after sender and receiver full
reconnects, including retention of registered handlers and methods.
The codec matrix sustains VP8, VP9, H264, and AV1 publish/subscribe with periodic frame-progress
checks; configure its per-codec duration with `-CodecSoakSeconds`.
The resource soak additionally samples handle, thread, and Private Bytes growth; configure its
duration with `-SoakSeconds` and retain a credential-free summary with `-ResultLogPath`.
Integration failures append bounded gtest, unified SDK/transport, and server diagnostics to that
result log. Before cleanup, the harness rejects raw credentials, tokens, SDP, ICE candidates, or
credentialed TURN URLs and reports which SDK log sources were captured.
The weak-network scenario additionally requires `-ClumsyExecutable <path>` and an Administrator
PowerShell. It restricts clumsy to the harness-owned LiveKit loopback ports and verifies media and
reliable-data recovery after loss, latency, jitter, and a temporary outage.
`-VideoCodec vp8` is the default; VP8, H264, and AV1 are supported by the media and E2EE scenarios.
The E2EE scenario creates two short-lived identities and verifies encrypted audio, the selected
video codec, data, state events, key-slot switching, automatic key ratcheting, reconnect recovery,
and missing/wrong-key recovery. The harness verifies the listener's exact executable path before
stopping it and never writes credentials or logs into the repository.

For official JS interoperability, build the separately downloaded `client-sdk-js` and run:

```powershell
node .\test\integration\run_js_e2ee_interop.mjs `
  --official-js-sdk "C:\path\to\client-sdk-js" `
  --config "C:\path\to\livekit-server-config.yaml" `
  --video-codec h264
```

This launches a headless browser and verifies encrypted audio, the selected VP8 or H264 video, and
data from this SDK, plus encrypted data in the reverse direction. The official JS package remains
an external test fixture and is not linked into or distributed with this SDK.

The integration build can also create a peer linked only to the official C++ SDK v1.8.0. Point
`OFFICIAL_LIVEKIT_CPP_ROOT` at its extracted binary package, build
`livekit_official_cpp_e2ee_peer`, then run `run_reconnect_matrix.ps1` with
`-Scenario OfficialCpp`, `-OfficialCppPeerExecutable <path>`, and `-VideoCodec vp8` or
`-VideoCodec h264`. This verifies encrypted audio, video, and bidirectional data without linking the
official SDK into `livekitclient`. See [docs/E2EE.md](docs/E2EE.md) for the complete commands and
the AV1 interoperability boundary.

The old manual WebRTC test executable is excluded by default because it is not
deterministic; enable it only with `-DBUILD_LEGACY_TEST_TOOLS=ON`.

### Continuous integration

`.github/workflows/windows.yml` builds the SDK, examples, unit and functional suites, then builds
and runs the standalone consumer project. Automatic Windows runs are currently paused; start the
workflow explicitly with `workflow_dispatch` when needed. The regular workflow explicitly disables
H264 because the public fallback libwebrtc archive does not contain OpenH264.

The manually dispatched `.github/workflows/windows-integration.yml` runs the real room media and
E2EE matrix for VP8, H264, and AV1. Configure these repository variables before enabling it:

- `LIBWEBRTC_H264_URL`: ZIP containing an H264-enabled `include/` and `lib/` libwebrtc package.
- `LIVEKIT_SERVER_WINDOWS_URL`: ZIP containing `livekit-server.exe`.
- `LK_CLI_WINDOWS_URL`: ZIP containing `lk.exe`.
- `OFFICIAL_LIVEKIT_CPP_WINDOWS_URL` (optional): official C++ SDK ZIP; enables VP8/H264 interop.

The integration job is skipped when any required fixture URL is absent. It uses only ephemeral
local credentials generated inside the job; no API secret is stored in source or workflow files.

## Examples

See the [examples guide](examples/README.md) for build commands, arguments, environment variables,
and end-to-end audio, video, data-message, and file-transfer verification.

## C API

Pure C applications include `livekit/capi/livekit.h` and link the same `livekitclient` library.
The API uses opaque handles, caller-owned output buffers, and C function-pointer callbacks; no C++
type or exception crosses the ABI boundary. Synchronous failures expose a thread-local error
domain, code, and two-phase message reader. Remote participant snapshots provide immutable, owned
enumeration of publications and attached subscribed tracks; their child handles remain valid until
the root snapshot is destroyed. Incremental DataStream writers report one structured
completed/cancelled/failed callback, and `lk_room_perform_rpc_async()` delivers a borrowed
structured RPC result that can be cloned when it must outlive the callback. See the
[`c_sample`](examples/c_sample/sample.c) example for room creation, callback registration,
connection, snapshot enumeration, stream completion, subscribed codec/quality feedback, string
retrieval, and deterministic cleanup. The standalone
[`main.c`](test/consumer/main.c) consumer continuously verifies pure C compilation, C++ linkage,
runtime initialization, two-phase strings, structured errors, and cleanup.

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
