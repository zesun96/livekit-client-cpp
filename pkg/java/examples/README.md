# Java examples

The Java 17 examples use the same feature areas as the [Python examples](../../python/examples/README.md)
and [.NET examples](../../dotnet/examples/README.md). They use the JNI bridge and the matching
`livekitclient` shared library. `Examples` accepts one scenario name and reads `LIVEKIT_URL` and
`LIVEKIT_TOKEN`; set `LIVEKIT_EXAMPLE_SECONDS` to change the default ten-second duration.

| Scenario | What it demonstrates | Opens a physical device? |
| --- | --- | --- |
| `media-devices` | List capture devices and screen sources | No |
| `room-echo`, `room-events` | Join, send a greeting, and print events | No |
| `data-transfer` | Send data, text, bytes, and optionally a file path argument | No |
| `stream-writer` | Write an incremental text stream | No |
| `data-track-schema` | Store a JSON schema and publish a DataTrack frame | No |
| `token-source` | Supply credentials through a token callback | No |
| `rpc-receiver`, `rpc-caller` | Register and call `example.echo`; caller needs a destination identity | No |
| `e2ee` | Connect with a shared E2EE key from `LIVEKIT_E2EE_KEY` | No |
| `publish-audio`, `publish-video` | Publish a generated tone or RGBA frames | No |
| `receive-media`, `receive-media-events` | Read remote frames from readers or callbacks | No |
| `record-remote` | Record subscribed remote tracks into an existing output directory argument | No |
| `publish-microphone`, `publish-system-audio` | Capture an audio device; optional device ID argument | Yes |
| `publish-camera`, `publish-screen` | Capture a camera or screen; optional source ID argument | Yes |

Compile and run from the repository root after building both native libraries:

```powershell
$sources = rg --files pkg/java/src/main/java pkg/java/examples/java | Where-Object { $_ -like '*.java' }
javac -d out/build/java-example-classes $sources
$env:LIVEKIT_CLIENT_CPP_LIBRARY = 'C:\path\to\livekitclient.dll'
$env:Path = "$(Split-Path $env:LIVEKIT_CLIENT_CPP_LIBRARY);$env:Path"
$env:LIVEKIT_URL = 'ws://localhost:7880'
$env:LIVEKIT_TOKEN = '<participant token>'
java -Dlivekit.java.library='C:\path\to\livekitclient_java.dll' `
  -cp out/build/java-example-classes io.livekit.client.examples.Examples room-events
```

Run `rpc-receiver` first with one identity, then run `rpc-caller <receiver-identity> [payload]`
with another identity in the same room. Similarly, start `receive-media` or
`receive-media-events` before a synthetic publisher. `data-track-schema` requires server-side
participant data blobs. The token-source example reads a token from the environment for
illustration; production token sources should obtain fresh credentials from an authenticated
service. `e2ee` requires at least 32 UTF-8 bytes in `LIVEKIT_E2EE_KEY`.

On this repository's Windows layout, run `pkg/java/examples/run_local_smoke.ps1`. It starts the
local LiveKit server, creates temporary test credentials, compiles the examples, and runs
`TwoPeerSmoke` with `-Xcheck:jni`. The smoke checks two connected peers, data and byte messages,
incremental text and byte streams, RPC, DataTrack schema and frames, remote snapshots, synthetic
video publishing, frame callbacks, and the remote video reader. Use `-NativeDirectory` and
`-BridgeDirectory` if the DLLs are elsewhere. Pass `-Example e2ee`, `-Example publish-video`,
`-Example receive-media`, `-Example receive-media-events`, `-Example record-remote`, `-Example rpc`,
or another device-free scenario listed in the script to run it with temporary credentials.
Receiver scenarios start a second Java process. The runner opens no physical capture device and
stops the local server during cleanup.

The four device capture examples require usable hardware and are not part of the local smoke.
