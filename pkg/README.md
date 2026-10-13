# Language binding API support

This matrix covers language bindings built on the stable C ABI. The C and C++ APIs themselves are
documented in the [SDK feature guide](../docs/FEATURES.md). `Yes` means the binding exposes the
capability, `Partial` means some related APIs are available, and `No` means it does not. Zig
exposes the complete C ABI as low-level declarations, with owned-handle wrappers for common
room, media, stream, RPC, snapshot, E2EE, and DataTrack operations.
Swift has no implementation yet. The [Java binding](java/README.md) covers room lifecycle and
callbacks, token sources, participant and publication snapshots, messaging and streams, RPC,
E2EE, DataTrack basics, local media, physical capture, and bounded remote media readers. The
[.NET binding](dotnet/README.md) covers
room, media, data, stream, RPC, device, recording, and diagnostic operations.

| API capability | [Lua](lua/README.md) | [Python](python/README.md) | [.NET](dotnet/README.md) | [Java](java/README.md) | Swift | [Zig](zig/README.md) |
| --- | --- | --- | --- | --- | --- | --- |
| SDK version | Yes | Yes | Yes | Yes | No | Yes |
| Room creation and cleanup | Yes | Yes | Yes | Yes | No | Yes |
| Synchronous room connect and disconnect | Yes | No | Yes | Yes | No | Yes |
| Language-native asynchronous room connect and disconnect | Yes | Yes | Yes | Yes | No | No |
| Dynamic token source and advanced ICE connection options | Yes | Yes | Yes | Yes | No | Yes |
| Connection state, room SID, name, and metadata getters | Yes | Yes | Yes | Yes | No | Yes |
| Connection and participant callbacks | Yes | Yes | Yes | Yes | No | Yes |
| Receive data messages | Yes | Yes | Yes | Yes | No | Yes |
| Publish data messages synchronously | Yes | No | Yes | Yes | No | Yes |
| Publish data messages asynchronously | Yes | Yes | Yes | Yes | No | No |
| Local participant and remote participant snapshots | Yes | Yes | Yes | Yes | No | Yes |
| Local participant metadata and attributes | Yes | Yes | Yes | Yes | No | Yes |
| Remote track subscription and settings | Yes | Yes | Yes | Yes | No | Yes |
| Track subscription permissions and feedback events | Yes | Yes | Yes | Yes | No | Yes |
| External audio/video tracks, frame push, and remote frame streams | Yes | Yes | Yes | Partial | No | Yes |
| Audio/video frame events with bounded queues and video metadata | Yes | Yes | Yes | Partial | No | Yes |
| Microphone, system audio, camera, and screen device capture | Yes | Yes | Yes | Partial | No | Yes |
| Text and byte streams, chat, and file transfer | Yes | Yes | Yes | Yes | No | Yes |
| DataTrack schema, publishing, and subscription | Yes | Yes | Yes | Partial | No | Yes |
| RPC calls and method handlers | Yes | Yes | Yes | Yes | No | Yes |
| E2EE (C ABI GCM, keys, frame cryptors) | Yes | Yes | Yes | Yes | No | Yes |
| Media devices and remote recording | Yes | Yes | Yes | Yes | No | Yes |
| Logging, tracing, and RTC statistics | Yes | Yes | Yes | Partial | No | Yes |

Java's asynchronous room and data calls use `CompletableFuture` to run synchronous native calls
on an executor. Its audio/video frame listeners run directly on native media threads; Java does
not provide the bounded callback queues described by the frame-event row.

## Known Java parity gaps

| Area | Current Java API | Available in the C ABI or other bindings |
| --- | --- | --- |
| RTC stream statistics | `LocalTrack.rtcStatsJson()` | Normalized local and remote RTC stream snapshots; Java has no remote RTC stats getter. |
| DataTrack subscriptions | Publish, subscribe with buffer capacity, read, and close | Target FPS, maximum partial frames, subscription updates, reader close state and dropped-frame count. |
| Diagnostics callbacks | Log levels, trace options, and JSON trace files | Process-wide log and trace callback sinks with copied records. |
| Local media publication | Basic track source, screen share, mute, I420 capture | Track publish codec, simulcast, encoding, metadata and degradation options; richer video input formats and frame metadata. |
| Microphone processing | Echo cancellation, gain control, and noise suppression toggles | Detailed processing options and processing statistics. |
| Frame callback delivery | Direct listener calls on native media threads | Bounded queues, dropped-event counters, and a dedicated callback-error channel in other bindings. |
| Asynchronous operations | Room connect/disconnect, data publish, and RPC futures | Other bindings expose async variants for more stream, media, and DataTrack operations. |
| Error details | `LiveKitException` exposes status and message | Native error domain/code, including DataTrack-specific errors. |

Java's offline JNI smoke test covers loading, local snapshots, and disconnected-room behavior.
Two-peer transfer, remote snapshot, physical capture, and callback delivery still need integration
validation. Physical audio tests require the host-device procedure in the repository instructions.

Lua covers one-shot and incremental outgoing streams, writer progress and completion events,
and incoming completed and incremental stream events.
It also covers DataTrack, RPC, E2EE key control, device enumeration and speaker control, recording
status, and local/remote track RTC snapshots.
Lua exposes remote-track recording, bounded media, logging and tracing record queues, and JSON
trace output. Lua asynchronous calls use a native worker thread and resume coroutines when the Lua
thread calls `room:poll()` or `room:step()`.
