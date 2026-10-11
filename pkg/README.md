# Language binding API support

This matrix covers language bindings built on the stable C ABI. The C and C++ APIs themselves are
documented in the [SDK feature guide](../docs/FEATURES.md). `Yes` means the binding exposes the
capability, `Partial` means some related APIs are available, and `No` means it does not. Zig
exposes the complete C ABI as low-level declarations, with owned-handle wrappers for common
room, media, stream, RPC, snapshot, E2EE, and DataTrack operations.
Swift has no implementation yet. The [Java binding](java/README.md) currently covers room,
messaging, RPC calls, shared-key E2EE, and synthetic local media. The
[.NET binding](dotnet/README.md) covers
room, media, data, stream, RPC, device, recording, and diagnostic operations.

| API capability | [Lua](lua/README.md) | [Python](python/README.md) | [.NET](dotnet/README.md) | [Java](java/README.md) | Swift | [Zig](zig/README.md) |
| --- | --- | --- | --- | --- | --- | --- |
| SDK version | Yes | Yes | Yes | Yes | No | Yes |
| Room creation and cleanup | Yes | Yes | Yes | Yes | No | Yes |
| Synchronous room connect and disconnect | Yes | No | Yes | Yes | No | Yes |
| Coroutine asynchronous room connect and disconnect | Yes | Yes | Yes | Partial | No | No |
| Dynamic token source and advanced ICE connection options | Yes | Yes | Yes | No | No | Yes |
| Connection state, room SID, name, and metadata getters | Yes | Yes | Yes | Yes | No | Yes |
| Connection and participant callbacks | Yes | Yes | Yes | Partial | No | Yes |
| Receive data messages | Yes | Yes | Yes | Yes | No | Yes |
| Publish data messages synchronously | Yes | No | Yes | Yes | No | Yes |
| Publish data messages asynchronously | Yes | Yes | Yes | Yes | No | No |
| Local participant and remote participant snapshots | Yes | Yes | Yes | Partial | No | Yes |
| Local participant metadata and attributes | Yes | Yes | Yes | Partial | No | Yes |
| Remote track subscription and settings | Yes | Yes | Yes | Partial | No | Yes |
| Track subscription permissions and feedback events | Yes | Yes | Yes | No | No | Yes |
| External audio/video tracks, frame push, and remote frame streams | Yes | Yes | Yes | Partial | No | Yes |
| Audio/video frame events with bounded queues and video metadata | Yes | Yes | Yes | No | No | Yes |
| Microphone, system audio, camera, and screen device capture | Yes | Yes | Yes | No | No | Yes |
| Text and byte streams, chat, and file transfer | Yes | Yes | Yes | Partial | No | Yes |
| DataTrack schema, publishing, and subscription | Yes | Yes | Yes | No | No | Yes |
| RPC calls and method handlers | Yes | Yes | Yes | Yes | No | Yes |
| E2EE (C ABI GCM, keys, frame cryptors) | Yes | Yes | Yes | Partial | No | Yes |
| Media devices and remote recording | Yes | Yes | Yes | Partial | No | Yes |
| Logging, tracing, and RTC statistics | Yes | Yes | Yes | Partial | No | Yes |

Lua covers one-shot and incremental outgoing streams, writer progress and completion events,
and incoming completed and incremental stream events.
It also covers DataTrack, RPC, E2EE key control, device enumeration and speaker control, recording
status, and local/remote track RTC snapshots.
Lua exposes remote-track recording, bounded media, logging and tracing record queues, and JSON
trace output. Lua asynchronous calls use a native worker thread and resume coroutines when the Lua
thread calls `room:poll()` or `room:step()`.
