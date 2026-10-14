# livekit-client Java binding

This Java 17 binding calls the stable C ABI in `include/livekit/capi/livekit.h` through a small JNI
bridge. Build a shared `livekitclient` first, then build the bridge. The Java library itself has no
third-party dependencies.

On Windows, build `livekitclient.dll` with `-DLKC_MSVC_RUNTIME=static`. Some Java distributions
ship an older `msvcp140.dll` beside `java.exe`; a dynamically linked SDK can load that runtime and
crash while enumerating media devices. The static runtime build avoids that process-wide DLL
collision. The JNI bridge also links the static MSVC runtime. Use the matching static-runtime
libwebrtc and dependency packages when configuring the SDK.

```powershell
cmake -S pkg/java -B out/build/java-binding -G "Visual Studio 17 2022" -A x64 `
  -DLIVEKIT_CLIENT_CPP_LIBRARY="C:/path/to/livekitclient.lib"
cmake --build out/build/java-binding --config Release
javac -d out/build/java-classes (rg --files pkg/java/src/main/java pkg/java/src/test/java | Where-Object { $_ -like '*.java' })
$env:LIVEKIT_CLIENT_CPP_LIBRARY = 'C:\path\to\livekitclient.dll'
$env:Path = "$(Split-Path $env:LIVEKIT_CLIENT_CPP_LIBRARY);$env:Path"
java -Dlivekit.java.library="C:\path\to\livekitclient_java.dll" -cp out/build/java-classes io.livekit.client.OfflineSmoke
```

On Linux or macOS, pass the built `liblivekitclient.so` or `.dylib` to CMake. If CMake cannot
discover the JDK, set `JAVA_JNI_INCLUDE_DIR` to the directory containing its `jni.h` and
`jni_md.h`. Both native libraries must be built for the same architecture and compatible SDK
version. The OS loader must also find the native SDK's dependencies.

The public API is `io.livekit.client.Room`. It owns the native room and must be closed. Register
listeners before connecting. Listeners run on native SDK callback threads, receive copied data,
and should return promptly. Blocking operations have `CompletableFuture` alternatives. The
[Java examples](examples/README.md) include feature scenarios and a local two-peer smoke test.
The binding currently covers room lifecycle and state, local participant identity and profile,
local participant attributes and recording state,
local and remote participant snapshots with audio level, connection quality and speaking state,
detached participant permission, attribute and remote publication details,
remote publication subscription, encryption and subscribed-track state snapshots,
data messages, text/byte/file sending and receiving,
chat sending and editing, DTMF, track subscription, RPC calls and handlers, shared-key E2EE controls, synthetic audio
and video sources, microphone, system audio, camera, and screen capture sources, local tracks,
remote track snapshots and bounded audio/video stream readers,
remote track publish, mute, and subscription events,
remote track subscription and playback settings,
local RTC stats JSON, device and screen-source enumeration,
speaker controls and audio playback statistics,
connection options including ICE servers and GCM E2EE, DataTrack publishing, remote
snapshots, bounded readers, remote track recording, and outgoing incremental text/byte writers.
`E2eeOptions` also exposes unencrypted magic bytes, ratchet window, failure tolerance, and key
ring size; the shorter constructors retain SDK defaults.
Incoming incremental text/byte events can be registered by topic. DataTrack schemas and custom
frame encodings are supported. Diagnostics exposes process-wide log levels, bounded log and trace
record queues, and JSON tracing.
`Room.connectWithTokenSource` supplies credentials again when the SDK requests a refresh.
Pass `TokenFetchOptions` to supply room, participant, agent, deployment, and attribute fields
to the provider on initial connection and refresh.
An optional `ConnectionOptions` argument applies ICE, subscription, retry, and E2EE settings to
token-source connections as it does to ordinary connections.
`ConnectionOptions.reconnectPolicy` runs on the native recovery thread. Return a delay in
milliseconds or `null` to stop retries; callbacks should return promptly.
Its callback can run on the native recovery thread; return promptly and avoid blocking room
operations from inside the callback. A thrown exception or empty credentials fails that fetch.
`Room.Listener` reports room metadata, SID, recording status, token refresh, end of session,
transcription segments with their timing and finality, participant changes, and remote track
lifecycle events.
Chat callbacks preserve optional edit timestamps.
Text callbacks preserve reply IDs, attached stream IDs, and attributes; byte and file callbacks
preserve attributes. Message metadata is detached before the native callback returns.
Local track publication, unpublication, and subscription callbacks carry detached publication
snapshots.
Subscribed quality updates include copied per-codec quality lists.
Metrics callbacks provide time series, event samples, normalized timestamps, and string-table
indexes.
Register `Room.FrameListener` to receive copied audio and video frames with video metadata.
Frame delivery uses a dedicated Java thread and a bounded queue. `Room(int frameQueueCapacity)`
sets its capacity (default 32); `droppedFrameCallbacks()` counts frames discarded when full.
`drainFrameCallbackErrors()` retrieves listener failures from a bounded error queue, and
`droppedFrameCallbackErrors()` counts failures discarded when that queue is full. Return
promptly from frame listeners. Native frame callbacks are enabled only while a listener is
registered.
Participant metadata, name, and attribute change callbacks include copied participant data;
an empty attribute value means that key was removed.
Participant permission change callbacks include detached old and new permission snapshots.
`setTrackSubscriptionPermissions` accepts per-participant allowed track SIDs and can be called
before connecting; the native room retains the policy for signaling.
Connection quality, active speaker, and participant-list updates are available through
`Room.Listener`; speaker and participant lists are immutable snapshots.
`Room.Listener` also reports SIP DTMF, data channel backpressure, and E2EE cryptor state.
E2EE controls include shared and participant keys, key removal, ratcheting, and the data key index.
Frame cryptors can be enabled per track and direction, switched to another key index, or enabled
for all tracks from one participant.
`Room.frameCryptors()` returns detached cryptor state snapshots when E2EE is configured.
Room update and move callbacks provide a detached `RoomSnapshot`.
DataTrack publish and unpublish callbacks include detached schema and encoding metadata;
`onDataTrackFrame` supplies copied frame bytes and optional user timestamps.
Track subscription feedback includes permission, failure, stream state, and subscription status;
integer values match the corresponding `lk_*` enums in `include/livekit/capi/livekit.h`.

`AudioSource.createMicrophone`, `AudioSource.createSystemAudio`, `VideoSource.createCamera`, and
`VideoSource.createScreen` open host devices when called. Start or stop capture with the source's
`startCapture()` and `stopCapture()` methods. Use `LocalTrack.publishScreenShare()` for screen
video and system audio. A microphone source exposes `microphoneProcessingOptions()`,
`setMicrophoneProcessingOptions(...)`, and `microphoneProcessingStats()`; unavailable
measurements in the statistics record are `null`. Hardware capture has not been exercised by
the offline smoke test. Synthetic audio sources expose `queuedDurationMs()`, `clearQueue()`,
and `waitForPlayout(timeoutMs)`. `VideoSource.captureFrame(VideoFrameInput)` accepts all C ABI
video formats, optional plane layouts, rotation, and copied user metadata.
`TrackPublishOptions` controls codec, simulcast, encodings, frame metadata, and degradation
settings for ordinary and screen-share publication; shorter publish methods retain native defaults.

Use `Room.remoteTrackPublications()` to find a participant identity and track SID, then call
`createAudioStream` or `createVideoStream`. Read with `readFor(timeoutMs)` and close the stream
when done. A read returns `null` on timeout or when the remote track closes. Frames are copied
into Java arrays; `droppedFrames()` reports frames discarded by the bounded native queue.
`LocalTrack.rtcStats()` and `Room.remoteRtcStats(identity, sid)` return detached, normalized
`RtcTrackStats` snapshots. Optional RTC measurements are `null` when unavailable.
Use `startTrackRecording` with the same identity and SID to write a remote track to a file. Call
`stop()` before reading the file, then close the recorder.

Use `Room.streamText` or `Room.streamBytes` for outgoing incremental streams. Write chunks with
the returned writer and close it to complete the stream, or call `cancel(reason)` to abandon it.
Closing the room cancels any remaining writers.
The `StreamWriterOptions` overloads set destinations, attributes, stream ID, total size, chunk
size, and compression. Text streams can also set a reply ID, attached stream IDs, update flag,
and version. The shorter overloads retain the default 15,000 byte chunk size.
Writer `info()` returns a detached metadata snapshot and `isClosed()` reports its current state.
Pass a `StreamWriterListener` in `StreamWriterOptions` to receive progress after successful writes
and one completion result after close, cancel, write failure, or room shutdown. Listener methods
run on the calling thread; failures are sent to that thread's uncaught exception handler.
The `DataSendOptions` overloads of `sendText`, `sendBytes`, and `sendFile` expose destinations,
attributes, chunk size, and compression. Text sends also support replies and attachments;
byte sends support MIME type and name; file sends support MIME type.
`sendChatMessage` returns the message ID and original timestamp; pass that value to
`editChatMessage` to edit it later. `sendChat` still returns just the ID.
Register an incoming topic with `registerTextStreamHandler` or `registerByteStreamHandler` and
receive copied open, chunk, closed, and failed events through `Room.Listener`. Unregister the
topic when it is no longer needed.
Stream events include MIME type, optional total size, timestamp, and attributes. Text stream
events also include reply and attached-stream IDs.

`Diagnostics` controls process-wide logging and tracing. Start a JSON trace with
`startTraceJson(path)` and call `stopTrace()` to flush it. Trace output is written to the given
path by the native SDK. `traceOptions()` reports the current enabled state and category mask.
`setLogCaptureCapacity(n)` and `setTraceCaptureCapacity(n)` install process-wide callback
sinks. `drainLogRecords()` and `drainTraceRecords()` return copied records; the corresponding
`dropped*Records()` counters report overflow. A capacity of zero disables capture. Setting
either sink replaces another binding's sink of the same kind in this process.

DataTrack frame encoding values match `lk_data_track_frame_encoding_t` in the C ABI; use `0` for
unspecified. A `RemoteDataTrack` snapshot supplies the participant identity and SID needed by
`Room.subscribeDataTrack`, along with frame encoding and schema metadata. Pass
`DataTrackSubscriptionOptions` to set target FPS, buffer capacity, and maximum partial frames;
`Room.updateDataTrackSubscription` changes these options for an existing subscription. Close the
returned reader and local publisher when done. A reader returns `null` when its timeout expires
or the track closes, and exposes `isClosed()` and `droppedFrames()`.
Use `storeDataTrackSchema`, `getDataTrackSchema`, and `DataTrackPublishOptions` to associate
schemas and custom encodings with published DataTracks.
`LocalDataTrack.info()` and `RemoteDataTrack` expose publisher handles, E2EE state, encoding,
schema, and publication state.
`LiveKitException` exposes `status()`, `errorDomain()`, and `errorCode()`; DataTrack failures
use domain 2 and their `lk_data_track_error_code_t` value.

## Validation status

Java provides `CompletableFuture` variants for room connection, data and stream sending,
incremental writers, DataTrack publishing and reading, track publication, frame capture, and
remote media readers. These run synchronous native calls on an executor; cancelling a future
cannot interrupt a native call already in progress.
The offline JNI smoke test covers loading, local snapshots, disconnected-room behavior,
diagnostics queue setup, video input, audio queue controls, bounded frame callback delivery, and
asynchronous error propagation. The local two-peer smoke validates data and byte messages,
incremental streams, RPC, DataTrack frames and schemas, remote snapshots, synthetic video frames,
a remote video reader, and copied native trace records. Physical capture still needs
integration validation. Physical audio tests require the host-device procedure in the repository
instructions.

```java
try (Room room = new Room()) {
    room.addListener(new Room.Listener() {
        @Override public void onDataReceived(DataPacket packet) {
            System.out.println(packet.participantIdentity() + ": " + packet.data().length);
        }
    });
    room.connect(System.getenv("LIVEKIT_URL"), System.getenv("LIVEKIT_TOKEN"));
    room.publishData("hello".getBytes(java.nio.charset.StandardCharsets.UTF_8), "greeting", true);
}
```

For an offline smoke test, compile `src/test/java` alongside the main classes and run
`io.livekit.client.OfflineSmoke` with the two library paths set. No server or media device is
opened.
