# LiveKit C++ examples

These examples exercise room connection, audio and video publishing and receiving, data messages,
and file transfer against a real LiveKit server.

## Build

Configure the SDK with examples enabled, then build the Release configuration:

```powershell
cmake -S . -B out/build/vs2022-x64-release `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static `
  -DLIBWEBRTC_ROOT=E:/path/to/libwebrtc `
  -DBUILD_EXAMPLES=ON

cmake --build out/build/vs2022-x64-release --config Release
```

Executables are generated below
`out/build/vs2022-x64-release/examples/<example>/Release/`.

## Connection arguments

Every example accepts the server URL and access token as its first two arguments:

```powershell
<example>.exe <url> <token> [options]
```

This SDK expects the LiveKit RTC endpoint, including `/rtc`:

```powershell
$url = "http://<livekit-host>:7880/rtc"
```

The URL and token can instead be supplied through environment variables:

```powershell
$env:LIVEKIT_URL = "http://<livekit-host>:7880/rtc"
$env:LIVEKIT_TOKEN = "<short-lived-participant-token>"
```

Generate tokens with different participant identities but the same room name when running a
publisher and receiver together. Never put API secrets or generated tokens in source files or
repository configuration.

## Examples

### `c_sample`

A pure C program using the opaque-handle C API in `livekit/capi/livekit.h`. It registers room,
participant, and track callbacks, connects, reads the local identity into a caller-owned buffer,
reports subscription failures (including unsupported codecs and missing tracks), and cleans up the
room and runtime. For remote video it requests medium quality at 24 FPS through
`lk_room_update_remote_track_settings()`, prints track state changes, and demonstrates the opaque
incremental text-stream writer and topic handler C APIs.

```powershell
& "out/build/vs2022-x64-release/examples/c_sample/Release/c_sample.exe" $url $token
```

The example source is compiled as C11; only its final link step uses the C++ linker because the SDK
implementation and libwebrtc are C++ libraries.

Set `LIVEKIT_DTMF_DIGIT` and optionally `LIVEKIT_DTMF_CODE` to publish a reliable SIP DTMF packet
after connecting. The C sample and `room_event` both print received SIP DTMF events.
Set `LIVEKIT_CHAT_MESSAGE` to send a structured reliable chat message and print its generated ID
and timestamp. Received messages and edits are reported by both examples.

### `cpp_sample`

Connects to a room, prints the local participant identity and SID, and disconnects.

```powershell
& "out/build/vs2022-x64-release/examples/cpp_sample/Release/cpp_sample.exe" $url $token
```

### `room_event`

Receives room events, subscribed and unsubscribed tracks, SFU pause/resume state, decoded PCM/I420
frames, data messages, structured chat messages, SIP DTMF events, and completed files. It also
reports transcription segments, `OnReconnecting`, and `OnReconnected`
while the SDK first attempts a
protocol-level signal resume and falls back to a full reconnect when required, and prints the
protocol-level reason when the room disconnects. Applications can query the same value later with
`RoomInterface::LastDisconnectReason()`. The optional third argument controls how many seconds to
listen and defaults to 30.

The receiver also prints DataChannel backpressure transitions. Reliable and lossy channels use
independent bounded queues; once a queue reaches the high-water mark, SDK sends wait for it to
drain below the low-water mark or fail after the bounded timeout.

```powershell
& "out/build/vs2022-x64-release/examples/room_event/Release/room_event.exe" `
  $url $receiverToken 30
```

Start this example before a publisher when verifying media or file reception.

### `publish_audio`

Publishes a synthetic mono 440 Hz, 48 kHz signed 16-bit PCM tone for five seconds.
The example explicitly unpublishes the track before destroying it, demonstrating the same
publish/unpublish lifecycle exposed by the official client SDKs.

```powershell
& "out/build/vs2022-x64-release/examples/publish_audio/Release/publish_audio.exe" `
  $url $publisherToken
```

Pass a receiver identity as the optional third argument to deny every other participant access to
the published audio. Permissions may be configured before connecting and are restored after a
signal resume or full reconnect:

```powershell
& "out/build/vs2022-x64-release/examples/publish_audio/Release/publish_audio.exe" `
  $url $publisherToken receiver-identity
```

The C sample provides the same behavior through `LIVEKIT_ALLOWED_SUBSCRIBER` and
`lk_room_set_track_subscription_permissions()`.

Receivers get `OnTrackSubscriptionPermissionChanged` when access changes. After access is restored,
an application that was unsubscribed can call `SetRemoteTrackSubscribed(..., true)` to subscribe
again.

For a subscribed remote video publication, `UpdateRemoteTrackSettings()` can request a maximum
quality or explicit dimensions, an FPS cap, a subscription priority, and temporary server-side
disablement. `TrackPublicationInterface::SubscriptionStatus()` distinguishes desired, subscribed,
and explicitly unsubscribed tracks. Room listeners receive `OnTrackUnsubscribed`,
`OnTrackStreamStateChanged`, and `OnTrackSubscriptionStatusChanged` without polling.

### `publish_video`

Publishes synthetic 640x360 I420 video at approximately 30 frames per second for five seconds.
The SDK encodes the frames as VP8 for transport.
It then unpublishes the local track and renegotiates before disconnecting.

The SDK automatically republishes local tracks after a full reconnect. Applications that change
common publish settings and intentionally want to rebuild every publisher sender can call
`LocalParticipantInterface::RepublishAllTracks()`. The C API exposes the same manual operation as
`lk_room_republish_all_tracks()`.

```powershell
& "out/build/vs2022-x64-release/examples/publish_video/Release/publish_video.exe" `
  $url $publisherToken
```

### `data_transfer`

Sends one-shot text, in-memory bytes, the selected file, and a two-write incremental text stream
using LiveKit data streams compatible with the official JS and Go SDKs. Run `room_event` first to
observe Open/Chunk/Closed delivery for the incremental stream.

```powershell
& "out/build/vs2022-x64-release/examples/data_transfer/Release/data_transfer.exe" `
  $url $publisherToken C:/path/to/file.bin
```

When connection settings are supplied through environment variables, set `LIVEKIT_FILE` as well:

```powershell
$env:LIVEKIT_FILE = "C:/path/to/file.bin"
& "out/build/vs2022-x64-release/examples/data_transfer/Release/data_transfer.exe"
```

### `rpc`

Registers `example.echo` and either waits as a receiver or calls another participant. Start the
receiver without a destination, then pass its printed identity to the sender:

```powershell
# Terminal 1
& "out/build/vs2022-x64-release/examples/rpc/Release/rpc.exe" $url $receiverToken

# Terminal 2
& "out/build/vs2022-x64-release/examples/rpc/Release/rpc.exe" `
  $url $senderToken receiver-identity "hello"
```

The C sample also registers `c.echo`. Passing a third argument invokes that method on the selected
participant and demonstrates the opaque `lk_rpc_result_t` lifecycle.

## End-to-end verification

Create two tokens for different identities in the same room. Run `room_event` with the receiver
token, then run one of the publishing examples in another terminal:

```powershell
# Terminal 1
& "out/build/vs2022-x64-release/examples/room_event/Release/room_event.exe" `
  $url $receiverToken 30

# Terminal 2
& "out/build/vs2022-x64-release/examples/publish_video/Release/publish_video.exe" `
  $url $publisherToken
```

The receiver reports track subscription and the first decoded audio or video frame. For
`data_transfer`, it reports one-shot text/bytes, incremental text chunks, and the completed file
size.
