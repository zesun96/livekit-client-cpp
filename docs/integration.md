# Integration testing and acceptance results

This document is the entry point for real-server, recovery, interoperability, hardware-media, and
long-running acceptance tests. It also records the credential-free results completed for the
Windows H264 release baseline through 2026-08-30.

The detailed scenario definitions and acceptance gates remain in
[Reliability and weak-network testing](RELIABILITY_TESTING.md). E2EE protocol and interoperability
details are in [E2EE](E2EE.md).

## Recorded acceptance baseline

- SDK: `livekit-client-cpp` 0.3.0.
- Platform: Windows x64, Visual Studio 2022, Release unless stated otherwise.
- WebRTC: locally packaged Release/Debug libwebrtc with H264 enabled.
- Server: a harness-owned local LiveKit Server, plus LiveKit Cloud for the room-migration gate,
  using short-lived test-only credentials.
- Logs: retained summaries were scanned for API keys, secrets, JWTs, SDP, ICE candidates, and
  credentialed TURN URLs.

These results describe the recorded environment; they are not a substitute for rerunning the
release-candidate gates on its target hardware and network.

## Completed matrix

| Area | Recorded result |
| --- | --- |
| Participant lifecycle | Four concurrent clients, join/leave convergence, duplicate-identity replacement, and same-identity rejoin passed |
| Recovery | Signal resume, ICE restart, forced full reconnect, token refresh, and server restart passed for C++ and C rooms |
| Room migration | LiveKit Cloud move notification, server token refresh, destination-room TokenSource fetch, full reconnect, new room SID, and post-reconnect data publish passed |
| Media and data recovery | Three H264 media-matrix iterations (12 real-server subtests), plus DataStreams, DataTrack state, and RPC recovery passed |
| E2EE | Encrypted audio/video/data, key-slot switching, ratcheting, reconnect, and missing/wrong-key recovery passed |
| Codec matrix | Sustained VP8, VP9, H264, and AV1 publish/subscribe frame progress passed |
| Weak network | Scoped packet loss, latency, jitter, and temporary-outage profiles passed |
| Resource soak | H264 30-minute and 2-hour gates passed within handle, thread, and Private Bytes limits |
| Hardware audio processing | Physical AEC, double-talk preservation, noise suppression, and zero-drop gates passed |
| Installed Windows package | Release C++ and Release/Debug C runtime smoke plus all C ABI export checks passed; Debug C++ static-CRT ABI caveat recorded below |
| External video formats | Eleven public input formats, odd dimensions, padded planes, C/C++ entry points, and RGBA real-server publication passed |

## Resource soak results

The H264 soak sampled the integration process once per second after media publication and reception
were established. Default limits were peak growth of 64 handles, 16 threads, and 256 MiB of Private
Bytes from the post-startup baseline.

| Duration | Handles baseline/final/peak | Threads baseline/final/peak | Private Bytes baseline/final/peak | Peak growth | Result |
| --- | --- | --- | --- | --- | --- |
| 30 minutes | 404 / 408 / 412 | 33 / 31 / 33 | 32,391,168 / 38,150,144 / 38,240,256 | 8 handles, 0 threads, 5.6 MiB | Pass |
| 2 hours | 404 / 410 / 413 | 33 / 31 / 33 | 33,226,752 / 40,411,136 / 40,411,136 | 9 handles, 0 threads, 6.9 MiB | Pass |

Both credential-free result logs passed the sensitive-data scan. A deliberately failing 30-second
resource gate was also used to verify structured diagnostics, log retention, credential scanning,
and cleanup behavior. The three-iteration media-recovery run captured `livekit`, `webrtc`, and
`websocket` diagnostics and also passed the audit.

## Hardware audio-quality result

The final physical test used an independent USB microphone as input and the USB speaker
`扬声器 (5- USBAudio2.0)` as output. The connected `Realtek(R) Audio` endpoint was headphones and
was not used for the acoustic path. The user also confirmed that microphone sound was physically
audible from the USB speaker during the end-to-end check.

| Gate | Recorded value | Required value | Result |
| --- | --- | --- | --- |
| Measured AEC ERLE | 36.9422 dB | at least 6 dB | Pass |
| Baseline/residual RMS | 281.189 / 46.5544 | residual lower than baseline | Pass |
| Baseline/residual median-frame RMS | 70.325 / 0.57735 | residual lower than baseline | Pass |
| Estimated AEC delay | 180 ms | diagnostic, no fixed gate | Recorded |
| Double-talk P90 retention | 0.821234 | at least 0.60 | Pass |
| Noise suppression ratio | 0.182707 | at most 0.75 | Pass |
| Clipped samples | 0 in the final combined run | zero | Pass |
| Processing/playback drops | 0 | zero | Pass |

The run captured unified `livekit`, `webrtc`, and `websocket` evidence and passed the sensitive-log
audit. AEC, AGC, and NS are provided by libwebrtc's Audio Processing Module through SDK-level
switches and defaults; the SDK does not implement separate acoustic algorithms.

## Windows package acceptance

The 0.3.0 DLL package was independently consumed through its installed
`LiveKitClientConfig.cmake`:

- Release and Debug C++ consumers built and reported `LiveKit SDK 0.3.0`; Release terminated
  normally, while Debug exposed the static-CRT teardown issue recorded below.
- Release and Debug pure C consumers built and reported `LiveKit C API 0.3.0`.
- Release and Debug DLLs each matched all 254 declarations in the public C header.
- The package contained configuration-matching `livekitclient[d].dll`, import libraries,
  `websockets.dll`, and the Debug PDB.

The separately generated 0.4.0 DLL archive passed Release C/C++ and Debug C runtime smoke tests and
matched all 258 C ABI declarations present at packaging time. Its Debug C++ smoke executable builds
and prints the version but does not terminate cleanly. The same behavior reproduces with the prior
0.3.0 archive and is the known static-CRT cross-DLL C++ ABI boundary, not a 0.4.0 source regression;
the C ABI remains the supported stable DLL boundary. A future Windows packaging pass must move the
C++ DLL configuration to a shared, matching MSVC runtime before advertising Debug C++ ABI support.

## Multi-format video-frame acceptance

After producing the 0.4.0 archive, the next source batch added RGBA, ABGR, ARGB, BGRA, RGB24, I420,
I420A, I422, I444, I010, and NV12 input with canonical or explicit plane layouts. The Windows
Release deterministic suite passed all 37 unit and 109 functional tests; the 31 opt-in integration
tests were skipped in that ordinary run. Release and Debug DLL builds each matched the resulting
260 C ABI declarations. The local `Media` scenario also passed all four real-server tests, including
640x360 RGBA publication, remote decoding, audio/video frame progress, and sensitive-log audit.

## Running the integration harness

Configure with `BUILD_INTEGRATION_TESTS=ON`, build `livekit_server_integration_tests`, and invoke the
Windows harness. Tokens are generated by a trusted backend or `lk token create`; never store real
API secrets in source, examples, CMake configuration, commands that will be logged, or result
files.

```powershell
cmake --build out/build/vs2022-x64-release --config Release `
  --target livekit_server_integration_tests --parallel

$apiSecret = Read-Host "LiveKit API secret"
.\test\integration\run_reconnect_matrix.ps1 `
  -ServerExecutable "C:\path\to\livekit-server.exe" `
  -LkExecutable "C:\path\to\lk.exe" `
  -ApiKey "devkey" -ApiSecret $apiSecret `
  -BuildDirectory "out\build\vs2022-x64-release" `
  -Configuration Release `
  -Scenario Participants -Iterations 3
```

Available scenarios are `Participants`, `Restart`, `TokenRefresh`, `Media`, `E2EE`, `CAPI`,
`DataRecovery`, `CodecMatrix`, `Soak`, `AudioQuality`, `WeakNetwork`, and `OfficialCpp`. Use
`-VideoCodec vp8`, `h264`, or `av1` where supported. The codec matrix also covers VP9.

The harness owns the temporary server it starts, uses short-lived identities, removes temporary
configuration and logs, and restores a replaced existing server in a `finally` block. It verifies
the exact executable path before stopping a listener.

## Long-running and disruptive scenarios

Run the 30-minute gate before the 2-hour gate:

```powershell
.\test\integration\run_reconnect_matrix.ps1 <common-arguments> `
  -Scenario Soak -VideoCodec h264 -SoakSeconds 1800 `
  -ResultLogPath "C:\path\to\resource-soak-30m.log"

.\test\integration\run_reconnect_matrix.ps1 <common-arguments> `
  -Scenario Soak -VideoCodec h264 -SoakSeconds 7200 `
  -ResultLogPath "C:\path\to\resource-soak-2h.log"
```

`WeakNetwork` requires an Administrator PowerShell and clumsy/WinDivert. Fault injection is limited
to harness-owned loopback ports and is cleared during cleanup. `AudioQuality` must run outside a
sandbox because it opens physical devices and emits an audible test signal. Notify nearby users
before starting it and explicitly select input/output IDs when the defaults do not form the desired
speaker-to-microphone path.

Neither disruptive scenario is part of ordinary unit/functional test execution.

## Room migration

Room migration requires a deployment whose Room Service implements `MoveParticipant`. The bundled
single-node LiveKit Server currently returns `not implemented` for this operation, so room migration
is deliberately excluded from the local `All` matrix. The dedicated gate passed against LiveKit
Cloud on 2026-08-30 using signalling protocol 17 and strict WSS certificate verification.

On a supported multi-node or cloud deployment, generate short-lived tokens for the same identity in
the source and destination rooms, then run:

```powershell
$apiSecret = Read-Host "LiveKit API secret"
.\test\integration\run_room_move_integration.ps1 `
  -ServerUrl "https://livekit.example.com" `
  -LkExecutable "C:\path\to\lk.exe" `
  -ApiKey "<api-key>" -ApiSecret $apiSecret `
  -SourceToken "<source-room-token>" `
  -DestinationToken "<destination-room-token>" `
  -SourceRoom "source-room" -DestinationRoom "destination-room" `
  -Identity "room-move-client"
```

The test verifies the moved room snapshot and server token-refresh event, then forces a full
reconnect and confirms that `TokenSourceFetchOptions::room_name` was changed to the destination
room before fetching replacement credentials. It also verifies that the destination room SID is
available after reconnect and that data can still be published. LiveKit Cloud may leave the SID
empty in the initial move notification; applications should treat the room name as authoritative at
that point and observe the later room update or reconnect for the new SID.

## Direct credential-based integration tests

When a server and short-lived tokens are already available, the integration executable reads:

- `LIVEKIT_URL`;
- `LIVEKIT_TOKEN_SINGLE`;
- `LIVEKIT_TOKEN` and `LIVEKIT_TOKEN_2` for two different identities;
- `LIVEKIT_TOKEN_2_UPDATE` for metadata/name/attribute permission checks; and
- `LIVEKIT_HARDWARE_MEDIA=1` to opt into real capture.

```powershell
$env:LIVEKIT_URL = "http://<livekit-host>:7880/rtc"
$env:LIVEKIT_TOKEN_SINGLE = "<unique-client-token>"
$env:LIVEKIT_TOKEN = "<first-client-token>"
$env:LIVEKIT_TOKEN_2 = "<second-client-token>"

ctest --test-dir out/build/vs2022-x64-release -C Release `
  -L integration --output-on-failure
```

## Interoperability

The official JavaScript interop runner launches a headless browser and verifies encrypted
audio/video/data in both directions:

```powershell
node .\test\integration\run_js_e2ee_interop.mjs `
  --official-js-sdk "C:\path\to\client-sdk-js" `
  --config "C:\path\to\livekit-server-config.yaml" `
  --video-codec h264
```

The optional `livekit_official_cpp_e2ee_peer` target links only the official C++ SDK and is invoked
with `-Scenario OfficialCpp`. See [E2EE](E2EE.md) for setup commands and the documented AV1
interoperability boundary.

## Release-candidate policy

The completed results above establish the current baseline. Before a release candidate:

1. rerun unit and functional suites;
2. rerun participant, recovery, data, E2EE, and codec scenarios;
3. run the 30-minute soak, followed by the 2-hour soak;
4. rerun weak-network profiles when network/reconnect code changed;
5. rerun hardware audio quality when capture, playback, or APM integration changed; and
6. retain only credential-free summaries and record the new environment and date here.
