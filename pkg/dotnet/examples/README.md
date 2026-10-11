# .NET examples

These examples cover the same feature areas as the [Python examples](../../python/examples/README.md)
and [Lua examples](../../lua/examples/README.md). Build the matching native
`livekitclient` shared library and set `LIVEKIT_CLIENT_CPP_LIBRARY` to its full
path. Connection examples read `LIVEKIT_URL` and `LIVEKIT_TOKEN`; `--url` and
`--token` override them. Tokens need room-join permission.

Run a scenario with:

```powershell
$env:LIVEKIT_CLIENT_CPP_LIBRARY = 'C:\path\to\livekitclient.dll'
$env:LIVEKIT_URL = 'ws://localhost:7880'
$env:LIVEKIT_TOKEN = '<participant token>'
dotnet run --project pkg/dotnet/examples/FeatureExamples -- media-devices
dotnet run --project pkg/dotnet/examples/FeatureExamples -- data-transfer
dotnet run --project pkg/dotnet/examples/RoomEcho -- --seconds 10
```

| Scenario | What it does | Opens a physical device? |
| --- | --- | --- |
| `RoomEcho` | Join, greet, and print data/chat | No |
| `media-devices` | List audio/video devices and screen sources | No |
| `room-events` | Log participant, track, data, chat, and stream events | No |
| `data-transfer` | Send text, bytes, optional file, and data | No |
| `stream-writer` | Write an incremental text stream | No |
| `data-track-schema` | Store JSON schema and publish a DataTrack frame | No |
| `token-source` | Supply credentials through a callback | No |
| `rpc-receiver`, `rpc-caller` | Register and call `example.echo` | No |
| `e2ee` | Connect using a shared E2EE key | No |
| `publish-audio`, `publish-video` | Publish synthetic tone or I420 frames | No |
| `receive-media` | Read remote audio/video frames from streams | No |
| `receive-media-events` | Receive remote frames through callbacks | No |
| `record-remote` | Save remote audio as WAV and video in its encoded format | No |
| `publish-microphone`, `publish-system-audio` | Capture microphone or output loopback | Yes |
| `publish-camera`, `publish-screen` | Capture camera or monitor | Yes |

Use `--seconds N` to set the duration of running scenarios. Start a receiver
first, then a publisher with a different identity in the same room. For RPC,
start `rpc-receiver --seconds 30`, copy its printed identity, then run
`rpc-caller --destination <identity> --payload hello`. `data-transfer` accepts
`--file <path>`, and `record-remote` requires an existing `--output-dir <path>`.
Device examples accept `--device-id <id>`; `publish-screen` accepts
`--source-id <id>` and otherwise selects the first monitor. Use `media-devices`
to find IDs. `data-track-schema` requires server-side participant data blobs.
`e2ee` reads `LIVEKIT_E2EE_KEY`, which must contain at least 32 UTF-8 bytes.
The token-source example uses environment variables for illustration; a real
application should fetch short-lived credentials from its own service.

The device examples open real hardware and are not part of the standard smoke
tests. Run `pkg/dotnet/examples/run_local_smoke.ps1` to start the local LiveKit
server and exercise the device-free scenarios with temporary test credentials.
The repository's `tests/ServerIntegration/run.ps1` verifies two .NET
clients, messaging, streams, RPC, synthetic video, and DataTrack without
opening physical devices.
