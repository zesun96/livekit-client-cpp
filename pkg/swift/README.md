# Swift binding

Swift Package Manager binding for the LiveKit C++ SDK's stable C ABI. Requires Swift 5.9 or
newer and a matching **shared** native SDK. Windows x64 was validated with Swift 6.4.
Linux and macOS await the native SDK ports; iOS and Android are not supported targets.
This package lives in the SDK source checkout and imports its canonical C header directly.
It does not download or bundle native binaries.

## Build and test on Windows

From the repository root, using the existing shared SDK build:

```powershell
./pkg/swift/build.ps1 -Action test
./pkg/swift/build.ps1 -Action build
# Override with another directory containing livekitclient.lib and livekitclient.dll:
./pkg/swift/build.ps1 -NativeDirectory C:/sdk/lib -Action test
```

The helper adds the native directory to the current process's DLL search path and passes
its import-library location to Swift. Build outputs go to `out/build/swift-cpp-binding`.
Tests are offline, use synthetic media, and do not open physical microphones or speakers.

Equivalent SwiftPM command (put the native DLL directory on `PATH` for running tests):

```powershell
swift test --package-path pkg/swift -Xlinker -L -Xlinker C:/sdk/lib
```

Add the local package to a consuming `Package.swift`:

```swift
dependencies: [.package(path: "../livekit-client-cpp/pkg/swift")],
targets: [
    .executableTarget(name: "App", dependencies: [
        .product(name: "LiveKitCpp", package: "swift")
    ])
]
```

Supply the same native library search path when building your app. Deploy the native DLL
and its dependencies along with the Swift runtime. The Swift module is named `LiveKitCpp`
to avoid a Windows filename collision with the native `livekitclient.lib`.

## Room and messages

```swift
import Foundation
import LiveKitCpp

try LiveKit.initialize()
let room = try Room()
room.onEvent { event in print(event) }
try await room.connectAsync(url: url, token: token)
// Wait for .connected or isConnected before publishing.
try await room.publishDataAsync(Data("hello".utf8), topic: "chat")
try await room.disconnectAsync()
room.close()
try LiveKit.shutdown()
```

Use `defer { room.close() }` or explicit error handling to close rooms on failure. Destroy
all sources and tracks before shutting down the runtime. `connect` and `disconnect` provide
synchronous equivalents; connection options use initialized `lk_room_connect_options_t`.
Async calls run on a serial worker queue and propagate copied native errors. Cancellation
does not abort native calls, and connect completion can precede the connected state event.

`Room` serializes native operations and offers state, room metadata, participant snapshots,
attribute updates, subscription settings/permissions, messaging, streams, and RPC. `RoomEvent`
covers connection lifecycle, participant updates, track lifecycle and subscription feedback,
recording state, encryption state, incoming messages, and media/DataTrack frames.
Event strings, attributes, sample buffers, and video user metadata are copied before native
callback return. Handlers run in admission order on a separate serial queue; keep them short.
Audio/video/DataTrack frame events share a bounded queue of 16 pending frames; configure it
with `configureMediaEvents(capacity:)`, or use zero to disable frame delivery. New frames are
dropped when full; `droppedMediaEvents` reports the count. Non-frame room events are not bounded.
Events already queued can arrive after close. Use weak captures when referencing a room in its
own event, RPC, token, or stream handler to avoid a retain cycle.

## Advanced APIs

| Area | Swift API |
| --- | --- |
| Token sources / ICE / reconnect / E2EE connection options | `connect(tokenSource:fetchOptions:options:)`, `connect(url:token:options:)` |
| Participants / publications / permissions / attributes | `localSnapshot()`, `remoteSnapshots()`, snapshot accessors, `setAttributes` |
| Remote subscription | `setSubscribed`, `updateTrackSettings`, `setSubscriptionPermissions` |
| Text / bytes / chat / DTMF / files | `sendText`, `sendBytes`, `sendChatMessage`, `editChatMessage`, `publishDTMF`, `sendFile` |
| Incremental outgoing streams | `streamText`, `streamBytes`, writers' `write`, `close`, `cancel`, `info` |
| Incremental incoming streams | `registerTextStream`, `registerByteStream`, copied `StreamEvent` values |
| RPC caller and receiver | `performRPC`, `performRPCAsync`, `registerRPC`, `unregisterRPC` |
| DataTrack | schema store/fetch, publish/push/unpublish, remote snapshots, subscription reader |
| E2EE | key set/export/ratchet/remove/clear, cryptor control, participant control, data key index |
| Devices and capture | `LiveKit.mediaDevices`, `screenSources`, source factories and start/stop/switch |
| Remote media and recording | `audioStream`, `videoStream`, `record` |
| RTC statistics | `LocalTrack.rtcStats`, `RemoteTrackSnapshot.rtcStats` |
| Logging / tracing | `Diagnostics.onLog`, `onTrace`, `startJSONTrace`, options setters |

Initialize native `lk_*_options_t` values with their corresponding `lk_*_options_init`
function. Pointer fields used in a synchronous call must stay valid through that call. Raw
callback fields supplied in advanced options require an independently retained callback context.
The convenience stream overloads manage progress/completion callback contexts automatically.
`FrameRead` distinguishes a received copied frame, a temporarily empty reader, and a closed
reader; native queue capacities and dropped-frame counters are exposed. Close readers and stop
recorders explicitly to unblock reads or flush recordings, then let ARC release their handles.

`NativeObject` wrappers destroy owned handles under ARC. Borrowed snapshot children retain
their parent snapshot, so they remain valid after the array or room goes away. Snapshots and
their string/attribute getters describe the captured value, rather than subsequent changes.
Source, track, stream, recorder, and snapshot methods must be serialized by the caller; these
objects do not declare Swift `Sendable` conformance.

RPC and token providers run synchronously on native threads; return quickly and avoid calling
room operations from those providers. Outgoing asynchronous RPC uses the room worker queue
and completes with a copied `RPCResponse`, including protocol errors. Distinct response strings
are interned until room close so native consumers can safely copy them after callback return.
Registered and subsequently unregistered callback contexts are also retained through room close
to cover invocations already in flight. Keep long-running rooms' response sizes bounded.
Diagnostics sinks are process-wide; unregister them before shutting down. Their copied callbacks
run on serial queues and already queued records may arrive after unregistration.

## Media and complete C ABI

`AudioSource` accepts interleaved Int16 PCM; `VideoSource` accepts initialized
`lk_video_frame_input_t` with borrowed plane buffers. After connecting, create tracks with
`room.createAudioTrack` / `room.createVideoTrack`, then publish using initialized
`lk_track_publish_options_t`. The room retains created tracks until close, and tracks retain
their sources. `LocalTrack` supports mute, unpublish, encoding updates, and RTC statistics.
`AudioSource.microphone` / `systemAudio` and `VideoSource.camera` / `screen` create device-backed
sources, with explicit start/stop/switch operations. `sampleRate` and `channels` describe external
PCM sources; device-backed audio sources report zero for these fields and capture their own PCM.

`import CLiveKit` exposes **every** C declaration without duplicated ABI definitions.
It is also re-exported by `LiveKitCpp`. The native API remains available for specialized options,
events, and controls beyond the convenience wrappers.
Use `room.withNativeHandle { handle in ... }` for operations needing the wrapped room.
The handle is borrowed only for the closure: do not destroy it or replace its callbacks.
Avoid re-entering `close` from that closure while still using the handle.
With raw C callbacks, copy borrowed data, retain callback contexts, observe thread rules,
and explicitly destroy owned handles as documented in `include/livekit/capi/livekit.h`.
Do not destroy a raw room from its native callback.

The [SwiftPM module map documentation](https://docs.swift.org/main/documentation/packagemanagerdocs/modulemaps/)
describes how the `CLiveKit` system library target imports the native header.

## Examples and integration tests

`Examples/Room` connects, prints events, publishes a data message, and disconnects. It reads
`LIVEKIT_URL` and `LIVEKIT_TOKEN` from the environment and exits clearly if either is absent:

```powershell
./pkg/swift/build.ps1 -Action run
```

`Examples/Features` covers devices, streams, DataTrack, token sources, RPC, E2EE, synthetic
publishing, remote readers, recording, and capture. See the [example guide](Examples/README.md)
for commands and required environment variables. Example source and linking were verified on
Windows; live server behavior and physical capture were not run as part of offline validation.

The double-participant integration fixture checks data delivery, RPC, and snapshot lifetime:

```powershell
# Supply LIVEKIT_URL and LIVEKIT_TOKEN_A/B for distinct identities in the same room.
$env:LIVEKIT_SWIFT_INTEGRATION = "1"
./pkg/swift/build.ps1 -Action test
```

Without this explicit switch or the required credentials, the integration test skips. No server
is started automatically. Capture examples open real devices and must be run outside a sandbox
that restricts physical media access.
