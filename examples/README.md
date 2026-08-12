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

### `cpp_sample`

Connects to a room, prints the local participant identity and SID, and disconnects.

```powershell
& "out/build/vs2022-x64-release/examples/cpp_sample/Release/cpp_sample.exe" $url $token
```

### `room_event`

Receives room events, subscribed tracks, decoded PCM/I420 frames, data messages, and completed
files. The optional third argument controls how many seconds to listen and defaults to 30.

```powershell
& "out/build/vs2022-x64-release/examples/room_event/Release/room_event.exe" `
  $url $receiverToken 30
```

Start this example before a publisher when verifying media or file reception.

### `publish_audio`

Publishes a synthetic mono 440 Hz, 48 kHz signed 16-bit PCM tone for five seconds.

```powershell
& "out/build/vs2022-x64-release/examples/publish_audio/Release/publish_audio.exe" `
  $url $publisherToken
```

### `publish_video`

Publishes synthetic 640x360 I420 video at approximately 30 frames per second for five seconds.
The SDK encodes the frames as VP8 for transport.

```powershell
& "out/build/vs2022-x64-release/examples/publish_video/Release/publish_video.exe" `
  $url $publisherToken
```

### `data_transfer`

Sends a reliable data message followed by the selected file as a chunked LiveKit data stream.

```powershell
& "out/build/vs2022-x64-release/examples/data_transfer/Release/data_transfer.exe" `
  $url $publisherToken C:/path/to/file.bin
```

When connection settings are supplied through environment variables, set `LIVEKIT_FILE` as well:

```powershell
$env:LIVEKIT_FILE = "C:/path/to/file.bin"
& "out/build/vs2022-x64-release/examples/data_transfer/Release/data_transfer.exe"
```

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
`data_transfer`, it reports both the data message and the completed file size.
