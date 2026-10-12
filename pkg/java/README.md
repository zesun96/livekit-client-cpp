# livekit-client Java binding

This Java 17 binding calls the stable C ABI in `include/livekit/capi/livekit.h` through a small JNI
bridge. Build a shared `livekitclient` first, then build the bridge. The Java library itself has no
third-party dependencies.

On Windows, build `livekitclient.dll` with `-DLKC_MSVC_RUNTIME=static`. Some Java distributions
ship an older `msvcp140.dll` beside `java.exe`; a dynamically linked SDK can load that runtime and
crash while enumerating media devices. The static runtime build avoids that process-wide DLL
collision. Use the matching static-runtime libwebrtc and dependency packages when configuring the
SDK.

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
binding currently covers room lifecycle and state, local participant identity and profile,
local participant attributes and recording state,
remote participant identity snapshots, data messages, basic text/byte/file sending and receiving,
chat, DTMF, track subscription, RPC calls and handlers, shared-key E2EE controls, synthetic audio
and video sources, microphone, system audio, camera, and screen capture sources, local tracks,
remote track snapshots and bounded audio/video stream readers,
remote track publish, mute, and subscription events,
remote track subscription and playback settings,
local RTC stats JSON, device and screen-source enumeration,
speaker controls and audio playback statistics,
connection options including ICE servers and GCM E2EE, and basic DataTrack publishing, remote
snapshots, bounded readers, remote track recording, and outgoing incremental text/byte writers.
Incoming incremental text/byte events can be registered by topic. DataTrack schemas and custom
frame encodings are supported. Diagnostics exposes process-wide log levels and JSON tracing.
`Room.connectWithTokenSource` supplies credentials again when the SDK requests a refresh.
Its callback can run on the native recovery thread; return promptly and avoid blocking room
operations from inside the callback. A thrown exception or empty credentials fails that fetch.
`Room.Listener` reports room metadata, SID, recording status, token refresh, and end of session
changes as well as remote track lifecycle events.
Participant metadata, name, and attribute change callbacks include copied participant data;
an empty attribute value means that key was removed.
Connection quality, active speaker, and participant-list updates are available through
`Room.Listener`; speaker and participant lists are immutable snapshots.
`Room.Listener` also reports SIP DTMF, data channel backpressure, and E2EE cryptor state.
Track subscription feedback includes permission, failure, stream state, and subscription status;
integer values match the corresponding `lk_*` enums in `include/livekit/capi/livekit.h`.

`AudioSource.createMicrophone`, `AudioSource.createSystemAudio`, `VideoSource.createCamera`, and
`VideoSource.createScreen` open host devices when called. Start or stop capture with the source's
`startCapture()` and `stopCapture()` methods. Use `LocalTrack.publishScreenShare()` for screen
video and system audio. Hardware capture has not been exercised by the offline smoke test.

Use `Room.remoteTrackPublications()` to find a participant identity and track SID, then call
`createAudioStream` or `createVideoStream`. Read with `readFor(timeoutMs)` and close the stream
when done. A read returns `null` on timeout or when the remote track closes. Frames are copied
into Java arrays; `droppedFrames()` reports frames discarded by the bounded native queue.
Use `startTrackRecording` with the same identity and SID to write a remote track to a file. Call
`stop()` before reading the file, then close the recorder.

Use `Room.streamText` or `Room.streamBytes` for outgoing incremental streams. Write chunks with
the returned writer and close it to complete the stream, or call `cancel(reason)` to abandon it.
Closing the room cancels any remaining writers.
Register an incoming topic with `registerTextStreamHandler` or `registerByteStreamHandler` and
receive copied open, chunk, closed, and failed events through `Room.Listener`. Unregister the
topic when it is no longer needed.

`Diagnostics` controls process-wide logging and tracing. Start a JSON trace with
`startTraceJson(path)` and call `stopTrace()` to flush it. Trace output is written to the given
path by the native SDK. `traceOptions()` reports the current enabled state and category mask.

DataTrack frame encoding values match `lk_data_track_frame_encoding_t` in the C ABI; use `0` for
unspecified. A `RemoteDataTrack` snapshot supplies the participant identity and SID needed by
`Room.subscribeDataTrack`. Close the returned reader and local publisher when done. A reader
returns `null` when its timeout expires or the track closes.
Use `storeDataTrackSchema`, `getDataTrackSchema`, and `DataTrackPublishOptions` to associate
schemas and custom encodings with published DataTracks.

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
