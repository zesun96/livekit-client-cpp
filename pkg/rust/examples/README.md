# Rust examples

Configure the native SDK and libclang paths as described in the [binding guide](../README.md).
Run commands below from `pkg/rust`. `cargo build --locked --examples` compiles every
scenario without connecting to a server or opening physical capture devices.

`media_devices` and `features -- diagnostics` work offline. Other examples read
`LIVEKIT_URL` and `LIVEKIT_TOKEN` from the environment and require permission to
join/publish in the selected test room. Examples never print tokens or E2EE keys.

| Command suffix after `cargo run --locked --example` | Scenario |
| --- | --- |
| `media_devices` | Device and screen-source enumeration |
| `room` | Connection, data/text/byte sending, queued event polling, disconnect |
| `async_room` | Standard Future-based connection, messaging, event reception and disconnect |
| `features -- streams` | Incremental text and binary writers, incoming stream events |
| `features -- rpc_receiver` | Register `rust.echo` and wait 30 seconds for calls |
| `features -- rpc_call` | Call `rust.echo` on `LIVEKIT_REMOTE_IDENTITY` |
| `features -- token_source` | Supply credentials through a provider that rereads the environment on refresh |
| `features -- e2ee` | Connect with the test passphrase in `LIVEKIT_E2EE_KEY` |
| `features -- data_track` | Store a JSON schema, publish a DataTrack, push timestamped frames |
| `features -- publish_media` | Publish generated PCM and I420 frames for about three seconds |
| `features -- remote_audio` | Read bounded audio frames without writing them to a speaker |
| `features -- remote_video` | Read bounded video frames and metadata |
| `features -- record` | Record a remote track for five seconds, then stop and report statistics |
| `features -- microphone` | Capture and publish a real microphone for five seconds |
| `features -- system_audio` | Capture and publish system audio for five seconds |
| `features -- camera` | Capture and publish a real camera for five seconds |
| `features -- screen` | Capture and publish a screen/window for five seconds |
| `features -- diagnostics` | Offline bounded log and trace queues |

All feature scenarios can optionally use `LIVEKIT_ICE_URL`, `LIVEKIT_ICE_USERNAME`,
and `LIVEKIT_ICE_PASSWORD`. RPC caller/receiver must use distinct participant
identities in the same room. Streams require another participant to receive them.
The token-source example demonstrates the callback contract; applications should
replace it with their own token service rather than reusing a fixed environment token.

Remote readers and recording require `LIVEKIT_REMOTE_IDENTITY` and
`LIVEKIT_REMOTE_TRACK_SID` for an already subscribed remote track. The `record`
scenario uses `LIVEKIT_RECORDING_PATH` or `target/livekit-record.wav`; select the
extension appropriate to the track's native recording format.

Capture examples open real devices. Select `LIVEKIT_DEVICE_ID` from enumeration
(a screen-source ID for `screen`); an empty ID selects the native default where
supported. Microphone capture uses native 48 kHz mono and system audio uses 48 kHz
stereo. On this Windows host, run physical audio capture examples outside the
sandbox as required by the workspace's `AGENTS.md`. These examples are compiled,
but are never run by the offline test command.

## Opt-in two-peer test

The six tests use supplied credentials for two distinct participants in one
isolated test room. They cover async messages/RPC/chat, stream writers and
progress/completion, DataTrack schema/frame readers, E2EE and detailed token
requests, generated media/frame metadata/WAV recording, owned snapshots, and
track/source/frame teardown. Physical capture has an additional hardware opt-in.
They are compiled but skipped by normal `cargo test` runs. To explicitly run them after
configuring `LIVEKIT_URL`, `LIVEKIT_TOKEN`, and `LIVEKIT_TOKEN_B`:

```powershell
$env:BUILD_INTEGRATION_TESTS = 'ON'
cargo test --locked --test two_peer -- --ignored --nocapture
```

Missing opt-in or credentials produce a clear skip message. Direct Cargo test
execution uses supplied credentials and does not start a server.
The SDK may open its normal playback device when remote media is subscribed;
follow the workspace's hardware execution rules when running it on this host.

The Windows runner reuses the workspace's existing LiveKit server executable,
generates temporary test-only JWT credentials in memory, starts a loopback-only
server, runs tests with a timeout, stops its own processes, and deletes the secret
configuration. Logs and generated recordings are kept under ignored `out/rust-smoke-*`.
Build first, then run the hardware/audio portion outside the sandbox on this host:

```powershell
cargo test --locked --test two_peer --no-run
./tests/run_local_smoke.ps1 -SkipBuild -IncludeHardware
```

`-BuildDirectory` selects the native SDK directory relative to the repository root.
`-TestFilter` selects one test, `-TestTimeoutSeconds` sets the suite deadline, and
`-Port` selects the local server port (the next two ports are reserved for RTC).
Without `-IncludeHardware`, the physical capture test prints a skip message.

The hardware test selects the independent USB microphone by its `USB Microphone`
label, selects the USB output containing `USBAudio2.0` for system-audio capture,
and verifies a camera and screen source. It records no microphone audio or screen
images and checks processing counters, state controls, and nonzero dimensions.
