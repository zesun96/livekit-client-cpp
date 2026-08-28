# Reliability and weak-network testing

The Windows reliability harness owns a local LiveKit Server process, creates short-lived tokens,
runs selected integration scenarios, and removes its temporary configuration and logs afterward.
It never writes credentials into the source or build tree.

## Participant lifecycle matrix

The `Participants` scenario currently verifies:

- four clients joining and leaving one room concurrently;
- observer snapshots and join/leave callbacks converging for every identity;
- a second connection replacing an existing participant with the same identity;
- the replaced client receiving `DisconnectReason::DuplicateIdentity`;
- the observer replacing the old SID with the new SID; and
- the same identity joining again after a normal disconnect with another new SID.

Build the integration target, then run three consecutive iterations:

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

The harness detects the default IPv4 route when permissions allow it. For a local-only run it
safely falls back to `127.0.0.1`; pass `-NodeIp` explicitly when testing clients on another host.
Each iteration must pass both participant tests. A failure includes the tail of the isolated test
logs without printing generated tokens.

## Repeated recovery matrix

`-Iterations` also controls the recovery scenario blocks:

- `TokenRefresh` performs a signal resume and a forced full reconnect while verifying that the
  server-refreshed token is retained and logs contain no credentials;
- `Media` forces publisher media failure, republishes audio after full recovery, resumes publisher
  and subscriber signaling with ICE restart, transfers data after each recovery, and then runs the
  selected video-codec media checks; and
- `Restart` repeats both the C++ and C API server-restart tests, preserving participant identity
  and transferring data after reconnection.

For example:

```powershell
.\test\integration\run_reconnect_matrix.ps1 <common arguments> `
  -Scenario TokenRefresh -Iterations 5
.\test\integration\run_reconnect_matrix.ps1 <common arguments> `
  -Scenario Media -VideoCodec h264 -Iterations 5
.\test\integration\run_reconnect_matrix.ps1 <common arguments> `
  -Scenario Restart -Iterations 5
```

Every iteration starts from newly created SDK room objects. Server-restart iterations reuse only
their short-lived room grant and restart the harness-owned server process between recovery checks.

The `DataRecovery` scenario verifies an incremental text stream and an RPC call before recovery,
after sender full reconnect, and after receiver full reconnect. It also proves that the receiver's
registered stream handler and RPC method remain installed across recovery:

```powershell
.\test\integration\run_reconnect_matrix.ps1 <common arguments> `
  -Scenario DataRecovery
```

## Sustained codec matrix

The `CodecMatrix` scenario publishes and subscribes VP8, VP9, H264, and AV1 sequentially. The
default duration is 30 seconds per codec. It requires a new received frame at every two-second
checkpoint, at least five received frames per second overall, and connected sender/receiver rooms
at the end of each run:

```powershell
.\test\integration\run_reconnect_matrix.ps1 <common arguments> `
  -Scenario CodecMatrix -CodecSoakSeconds 30
```

`-CodecSoakSeconds` accepts 5 through 7200 seconds. Use the dedicated resource soak described
below for long runs that also sample process resource growth.

## Resource soak

The `Soak` scenario samples the integration process once per second after the selected codec is
already publishing and receiving. It records baseline/final/peak handle, thread, and Private Bytes
values while retaining the codec frame-progress checks.

Run the 30-minute gate first, then the 2-hour gate:

```powershell
.\test\integration\run_reconnect_matrix.ps1 <common arguments> `
  -Scenario Soak -VideoCodec h264 -SoakSeconds 1800 `
  -ResultLogPath "C:\path\to\resource-soak-30m.log"

.\test\integration\run_reconnect_matrix.ps1 <common arguments> `
  -Scenario Soak -VideoCodec h264 -SoakSeconds 7200 `
  -ResultLogPath "C:\path\to\resource-soak-2h.log"
```

The defaults allow peak growth of 64 handles, 16 threads, and 256 MB of Private Bytes from the
post-startup baseline. Override them with `-MaxHandleGrowth`, `-MaxThreadGrowth`, and
`-MaxPrivateMemoryGrowthMb` only when the acceptance environment has documented reasons. A result
log contains no generated token or API secret.

The Windows Release H264 acceptance gates completed successfully. The 30-minute run recorded
baseline/final/peak values of 404/408/412 handles, 33/31/33 threads, and
32391168/38150144/38240256 Private Bytes; peak growth was 8 handles, 0 threads, and 5.6 MB. The
subsequent 2-hour run recorded 404/410/413 handles, 33/31/33 threads, and
33226752/40411136/40411136 Private Bytes; peak growth was 9 handles, 0 threads, and 6.9 MB. Both
credential-free result logs passed the sensitive-data scan.

## Failure diagnostics and credential audit

The integration executable installs a bounded asynchronous SDK logging sink before running a test.
It records LiveKit at debug level, WebRTC at warning level, and WebSocket at info level. Callbacks
only enqueue records; a worker batches them into the child process's stderr so diagnostic I/O does
not block WebRTC threads. On failure, the harness appends bounded, labelled gtest, SDK/transport,
and server log tails to `ResultLogPath` before removing its temporary directory.

Every harness exit scans temporary `*.log` files and the retained result log for the API key,
API secret, JWTs, authorization/access-token fields, raw SDP, ICE candidates, and credentialed TURN
URLs. A match fails the run without printing the matched value. Runs that started an SDK integration
process must also contain at least one `[livekit-sdk]` record; successful audits report the captured
source names. The Windows Release H264 media-recovery acceptance run captured
`livekit,webrtc,websocket` and passed the sensitive-data audit in three consecutive media-matrix
iterations (12 real-server subtests). An intentionally failing 30-second resource gate also
retained structured diagnostics and passed the audit and cleanup checks. SDK sanitization and the
harness audit additionally cover WebRTC's `Cand[]`, `Conn[]`, and `Port[]` ICE-detail formats.

## Weak-network matrix

Run the weak-network scenario from an Administrator PowerShell because clumsy opens the WinDivert
driver. The harness limits interception to loopback TCP ports `Port`/`Port + 1` and UDP port
`Port + 2`; it never applies a host-wide filter.

```powershell
$apiSecret = Read-Host "LiveKit API secret"
.\test\integration\run_reconnect_matrix.ps1 `
  -ServerExecutable "C:\path\to\livekit-server.exe" `
  -LkExecutable "C:\path\to\lk.exe" `
  -ClumsyExecutable "C:\path\to\clumsy.exe" `
  -ApiKey "devkey" -ApiSecret $apiSecret `
  -BuildDirectory "out\build\vs2022-x64-release" `
  -Configuration Release -Scenario WeakNetwork -WeakNetworkProfile All
```

The profiles are 15% packet loss for 8 seconds, 250 ms latency for 8 seconds, a 50-to-300 ms delay
change during an 8-second window to create jitter, and a 10-second 100% packet-loss outage. Every
profile proves that audio and reliable data work before the fault and resume after it. Use
`-WeakNetworkProfile Loss`, `Latency`, `Jitter`, or `Outage` to run one profile.

Each clumsy process has its own timeout and is also stopped by the harness `finally` block. The
harness then signals the integration test to verify recovery and removes all marker files, server
configuration, and isolated logs.

## Remaining matrix

| Area | Status | Next acceptance work |
| --- | --- | --- |
| concurrent participant lifecycle and duplicate identity | complete | increase participant count during soak runs |
| repeated resume, ICE restart, full reconnect, server restart | complete | raise iteration count during soak runs |
| packet loss, delay, jitter, and temporary outage | complete | repeat profiles during soak runs |
| DataTrack, DataStream, RPC, E2EE, and media across recovery | complete | repeat capability recovery during soak runs |
| VP8, VP9, H264, and AV1 sustained publish/subscribe | complete | repeat the matrix during soak runs |
| 30-minute and 2-hour soak with resource growth checks | complete | rerun both gates for release candidates |

Network mutation and long-running tests remain opt-in because they alter host networking or occupy
hardware and server resources. Ordinary unit and functional test runs never start or stop a server.
