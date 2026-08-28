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

## Remaining matrix

| Area | Status | Next acceptance work |
| --- | --- | --- |
| concurrent participant lifecycle and duplicate identity | complete | increase participant count during soak runs |
| repeated resume, ICE restart, full reconnect, server restart | complete | raise iteration count during soak runs |
| packet loss, delay, jitter, and temporary outage | pending | add an explicitly enabled network-fault adapter |
| DataTrack, DataStream, RPC, E2EE, and media across recovery | partial | combine capabilities into repeated recovery rounds |
| VP8, VP9, H264, and AV1 sustained publish/subscribe | partial | add duration and per-codec frame thresholds |
| 30-minute and 2-hour soak with resource growth checks | pending | record handles, threads, private bytes, and pass thresholds |

Network mutation and long-running tests remain opt-in because they alter host networking or occupy
hardware and server resources. Ordinary unit and functional test runs never start or stop a server.
