# livekit-client Java binding

This Java 17 binding calls the stable C ABI in `include/livekit/capi/livekit.h` through a small JNI
bridge. Build a shared `livekitclient` first, then build the bridge. The Java library itself has no
third-party dependencies.

```powershell
cmake -S pkg/java -B out/build/java-binding -G "Visual Studio 17 2022" -A x64 `
  -DLIVEKIT_CLIENT_CPP_LIBRARY="C:/path/to/livekitclient.lib"
cmake --build out/build/java-binding --config Release
javac -d out/build/java-classes (rg --files pkg/java/src/main/java pkg/java/src/test/java | Where-Object { $_ -like '*.java' })
$env:LIVEKIT_CLIENT_CPP_LIBRARY = 'C:\path\to\livekitclient.dll'
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
remote participant identity snapshots, data messages, basic text/byte/file sending and receiving,
chat, DTMF, track subscription, RPC calls and handlers, shared-key E2EE controls, synthetic audio
and video sources, local tracks, local RTC stats JSON, and screen-source enumeration. Advanced
connection options, physical device capture, remote media streams, incremental streams, DataTrack,
recording, and diagnostics are not yet exposed. The native C and C++ APIs provide those features.

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
