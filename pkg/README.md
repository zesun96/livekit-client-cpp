# Language binding API support

This matrix covers language bindings built on the stable C ABI. The C and C++ APIs themselves are
documented in the [SDK feature guide](../docs/FEATURES.md). `Yes` means the binding exposes the
capability, `Partial` means some related APIs are available, and `No` means it does not. Python,
Swift, and Zig bindings have no implementation yet.

| API capability | [Lua](lua/README.md) | Python | Swift | Zig |
| --- | --- | --- | --- | --- |
| SDK version | Yes | No | No | No |
| Room creation and cleanup | Yes | No | No | No |
| Synchronous room connect and disconnect | Yes | No | No | No |
| Coroutine asynchronous room connect and disconnect | Yes | No | No | No |
| Dynamic token source and advanced ICE connection options | Yes | No | No | No |
| Connection state, room SID, name, and metadata getters | Yes | No | No | No |
| Connection and participant callbacks | Yes | No | No | No |
| Receive data messages | Yes | No | No | No |
| Publish data messages synchronously | Yes | No | No | No |
| Publish data messages asynchronously | Yes | No | No | No |
| Local participant and remote participant snapshots | Yes | No | No | No |
| Local participant metadata and attributes | Yes | No | No | No |
| Remote track subscription and settings | Yes | No | No | No |
| Track subscription permissions and feedback events | Yes | No | No | No |
| External audio/video tracks, frame push, and remote frame streams | Yes | No | No | No |
| Microphone, system audio, camera, and screen device capture | Yes | No | No | No |
| Text and byte streams, chat, and file transfer | Partial | No | No | No |
| DataTrack schema, publishing, and subscription | Yes | No | No | No |
| RPC calls and method handlers | Yes | No | No | No |
| E2EE | Partial | No | No | No |
| Media devices and remote recording | Yes | No | No | No |
| Logging, tracing, and RTC statistics | Yes | No | No | No |

Lua covers one-shot and incremental outgoing streams and incoming completed stream events,
DataTrack, RPC, E2EE key control, device enumeration and speaker control, recording status, and
local/remote track RTC snapshots.
Lua exposes remote-track recording, bounded logging and tracing record queues, and JSON trace
output. Other advanced C API features remain unsupported. Lua asynchronous calls use a native
worker thread and resume coroutines when the Lua thread calls
`room:poll()` or `room:step()`.
