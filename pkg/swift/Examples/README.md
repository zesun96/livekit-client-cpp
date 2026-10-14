# Swift examples

Build from the repository root with `./pkg/swift/build.ps1 -Action build`. Run the simple room
example with `-Action run`, or select a feature:

```powershell
./pkg/swift/build.ps1 -Action run -Example livekit-features -ExampleArguments help
./pkg/swift/build.ps1 -Action run -Example livekit-features -ExampleArguments streams
```

Except `help` and `devices`, modes require `LIVEKIT_URL` and `LIVEKIT_TOKEN`. Use tokens for
the same room and distinct identities when running a publisher with a receiver. The programs
read credentials from the environment and do not print them or start a server.

| Mode | Behavior / additional environment |
| --- | --- |
| `devices` | Lists audio/video devices and screen sources; does not start capture |
| `data` | Sends data, text, bytes; optional `LIVEKIT_FILE_PATH` sends a file |
| `streams` | Registers incremental text/byte receivers and publishes incremental streams |
| `datatrack` | Stores a JSON schema, publishes a DataTrack frame, then unpublishes |
| `token` | Connects through a dynamic token-source callback |
| `rpc-server` | Registers `swift.echo` for 30 seconds |
| `rpc-call` | Calls `swift.echo`; requires `LIVEKIT_PEER_IDENTITY` |
| `e2ee` | Connects with native GCM E2EE; requires `LIVEKIT_E2EE_KEY` |
| `audio` | Publishes synthetic 48 kHz mono silence |
| `video` | Publishes generated 64x64 RGBA frames |
| `readers` | Pulls a remote frame; requires `LIVEKIT_PEER_IDENTITY`, `LIVEKIT_TRACK_SID`; optional `LIVEKIT_TRACK_KIND=video` |
| `recording` | Records a remote track for five seconds; also requires `LIVEKIT_RECORDING_PATH` |
| `microphone` / `system-audio` | Captures and publishes audio for five seconds; optional `LIVEKIT_DEVICE_ID` |
| `camera` / `screen` | Captures and publishes video for five seconds; select `LIVEKIT_DEVICE_ID` from device/screen enumeration |

Room and feature examples compile as part of `swift test`. Server and hardware modes are manual
and were not executed by the offline suite. The separate `IntegrationTests` fixture is opt-in;
it requires `LIVEKIT_SWIFT_INTEGRATION=1`, `LIVEKIT_URL`, and `LIVEKIT_TOKEN_A/B` and checks
two-participant data delivery, RPC, and snapshots surviving room destruction. Run it against a
local LiveKit server with test credentials. Physical capture modes must run outside the sandbox.
