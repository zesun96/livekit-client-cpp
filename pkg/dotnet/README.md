# livekit-client .NET binding

`LiveKit.Client` is a .NET 8 P/Invoke binding to the stable C ABI in
`include/livekit/capi/livekit.h`. It uses the shared `livekitclient` library and
does not compile native code as part of the C# project.

Build the C++ SDK as a shared library first. Set
`LIVEKIT_CLIENT_CPP_LIBRARY` to the **full path** of `livekitclient.dll` (or the
platform's shared library); make its native dependencies available to the OS
loader. The native binary and this binding must be built from compatible SDK
versions. Then run:

```powershell
dotnet build pkg/dotnet/LiveKit.Client/LiveKit.Client.csproj
$env:LIVEKIT_CLIENT_CPP_LIBRARY = 'C:\path\to\livekitclient.dll'
$env:LIVEKIT_URL = 'ws://localhost:7880'
$env:LIVEKIT_TOKEN = '<participant token>'
dotnet run --project pkg/dotnet/examples/RoomEcho/RoomEcho.csproj
```

An offline native smoke test (no server or media device) is available with
`dotnet run --project pkg/dotnet/tests/OfflineSmoke/OfflineSmoke.csproj` after
setting `LIVEKIT_CLIENT_CPP_LIBRARY`.

On Windows, run the local server integration test with
`pkg/dotnet/tests/ServerIntegration/run.ps1`. It reuses the local LiveKit server
and CLI, creates temporary test credentials, and tests two .NET participants,
data, text and incremental streams, RPC, synthetic video frame metadata,
DataTrack frames and custom schemas, and remote RTC statistics. It does not open
physical devices.

`Room` owns its native handle and must be disposed. It exposes synchronous and
task-based asynchronous connection, dynamic token providers, ICE options,
participant snapshots with permissions, attributes and subscribed track details,
data messages with optional destinations, chat, text/byte/file transfer,
incremental inbound text/byte stream handlers, synchronous and asynchronous RPC
and handlers, DataTrack schema/publish/subscribe, E2EE keys, and remote track
settings. `AudioSource`, `VideoSource`, `LocalTrack`, stream writers, remote
media readers, DataTrack readers, and `TrackRecorder` are also disposable.
Destroy writers/readers and tracks when finished; a room disposes its remaining
local track handles when destroyed.

All 54 C ABI room callback slots have managed events, including connection and
participant changes, track publication and subscription, active speakers,
permissions, transcription, metrics, E2EE state, room snapshots and SID changes,
DTMF, recording status, data and chat messages, completed text/byte/file
messages, audio/video frames, and DataTrack frames.
Event values are copied while
the native callback is active, then delivered on the .NET thread pool.
Audio/video/DataTrack frame event queues are capped at 32/8/32 pending deliveries and drop
new frames when full. Subscribers may run concurrently; subscriber exceptions
are delivered to `CallbackError`. Native operations throw `LiveKitException`
with the C status and error message; DataTrack operations throw
`DataTrackException`. RPC application/transport errors appear in `RpcResponse`.

Incoming text chunks expose both decoded `Content` and the exact `ContentBytes`;
use the bytes to reassemble UTF-8 text when a character spans chunks.
`TextSendRequest`, `ByteSendRequest`, and `FileSendRequest` configure destinations,
attributes, chunk size, and compression for one-shot transfers. Text sends also
accept reply and attached stream IDs. A zero chunk size uses the C default of
15,000 bytes. Received text and file/byte messages include detached attributes;
received text also includes attached stream IDs. `TextStreamRequest` and
`ByteStreamRequest` configure destinations, stream IDs,
attributes, total size, chunk size, compression, and progress/completion
callbacks on incremental writers. `GetInfo()` returns a detached writer info
snapshot. Stream callbacks are copied and delivered on the .NET thread pool;
subscriber failures are sent to the room's `CallbackError` event.
`VideoStream.Read()` returns copied frame metadata when available;
`VideoFrameWithMetadataReceived` delivers copied frame metadata through the
room event callback.
`VideoSource.CaptureI420()` also accepts user metadata.
`TrackPublishRequest` exposes source, codec, simulcast, encoding, metadata,
and degradation options for ordinary and screen-share track publication.

`MediaDevices` enumerates host devices and screen sources. Microphone capture
sources expose processing options and statistics. Rooms expose playback
statistics and the last disconnect reason. `Diagnostics`
configures log levels, trace categories, process-wide log/trace callbacks, and
JSON tracing. Callback records are copied before delivery on the .NET thread
pool; the queues are bounded and may drop records when full. `LocalTrack.RtcStatsJson` and
`LocalTrack.GetRtcStats()` expose the native RTC stats report. The room also
provides `GetRemoteRtcStats()`. Physical device
source creation opens the device;
run microphone, speaker, camera, and screen capture tests in an environment
that permits hardware access.

Push at least one video frame before publishing an external video track so its
source has dimensions. Use `LocalTrack.PublishWithFrameMetadata()` to forward
user timestamps, frame IDs, and user data to remote readers.

`ConnectE2ee` takes a shared key in bytes. Keep this key secret and retain it
only as long as needed. The caller still owns and should clear its original
byte array when appropriate. `GetFrameCryptors()` returns detached cryptor
snapshots for an E2EE configured room.

Data message publishing remains synchronous because the C ABI exposes only
`lk_room_publish_data`. Some C ABI helper functions, including raw snapshot
handle cloning and library shutdown, have no direct managed equivalent; the
[binding matrix](../README.md) tracks current coverage.
