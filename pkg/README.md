# Language binding API support

This matrix covers language bindings built on the stable C ABI. The C and C++ APIs themselves are
documented in the [SDK feature guide](../docs/FEATURES.md). `Yes` means the binding exposes the
capability, `Partial` means some related APIs are available, and `No` means it does not. Swift
and Zig bindings have no implementation yet.

| API capability | [Lua](lua/README.md) | [Python](python/README.md) | Swift | Zig |
| --- | --- | --- | --- | --- |
| SDK version | Yes | Yes | No | No |
| Room creation and cleanup | Yes | Yes | No | No |
| Synchronous room connect and disconnect | Yes | No | No | No |
| Coroutine asynchronous room connect and disconnect | Yes | Yes | No | No |
| Dynamic token source and advanced ICE connection options | Yes | Yes | No | No |
| Connection state, room SID, name, and metadata getters | Yes | Yes | No | No |
| Connection and participant callbacks | Yes | Partial | No | No |
| Receive data messages | Yes | Yes | No | No |
| Publish data messages synchronously | Yes | No | No | No |
| Publish data messages asynchronously | Yes | Yes | No | No |
| Local participant and remote participant snapshots | Yes | Partial | No | No |
| Local participant metadata and attributes | Yes | Yes | No | No |
| Remote track subscription and settings | Yes | Yes | No | No |
| Track subscription permissions and feedback events | Yes | Yes | No | No |
| External audio/video tracks, frame push, and remote frame streams | Yes | Partial | No | No |
| Audio/video frame events with bounded queues and video metadata | Yes | Yes | No | No |
| Microphone, system audio, camera, and screen device capture | Yes | Yes | No | No |
| Text and byte streams, chat, and file transfer | Yes | Partial | No | No |
| DataTrack schema, publishing, and subscription | Yes | Partial | No | No |
| RPC calls and method handlers | Yes | Yes | No | No |
| E2EE (C ABI GCM, keys, frame cryptors) | Yes | Partial | No | No |
| Media devices and remote recording | Yes | Partial | No | No |
| Logging, tracing, and RTC statistics | Yes | Yes | No | No |

Lua covers one-shot and incremental outgoing streams, writer progress and completion events,
and incoming completed and incremental stream events.
It also covers DataTrack, RPC, E2EE key control, device enumeration and speaker control, recording
status, and local/remote track RTC snapshots.
Lua exposes remote-track recording, bounded media, logging and tracing record queues, and JSON
trace output. Lua asynchronous calls use a native worker thread and resume coroutines when the Lua
thread calls `room:poll()` or `room:step()`.
