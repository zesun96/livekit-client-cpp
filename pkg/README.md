# Language binding API support

This matrix covers language bindings built on the stable C ABI. The C and C++ APIs themselves are
documented in the [SDK feature guide](../docs/FEATURES.md). `Yes` means the binding exposes the
capability; `No` means it does not. Python, Swift, and Zig bindings have no implementation yet.

| API capability | [Lua](lua/README.md) | Python | Swift | Zig |
| --- | --- | --- | --- | --- |
| SDK version | Yes | No | No | No |
| Room creation and cleanup | Yes | No | No | No |
| Synchronous room connect and disconnect | Yes | No | No | No |
| Coroutine asynchronous room connect and disconnect | Yes | No | No | No |
| Connection state, room SID, name, and metadata getters | Yes | No | No | No |
| Connection and participant callbacks | Yes | No | No | No |
| Receive data messages | Yes | No | No | No |
| Publish data messages synchronously | Yes | No | No | No |
| Publish data messages asynchronously | Yes | No | No | No |
| Audio and video tracks, sources, and frames | No | No | No | No |
| Text and byte streams, chat, and file transfer | No | No | No | No |
| RPC | No | No | No | No |
| E2EE | No | No | No | No |
| Media devices and remote recording | No | No | No | No |
| Logging, tracing, and RTC statistics | No | No | No | No |

Lua callbacks currently cover connected, reconnecting, reconnected, disconnected, connection-state
changes, participant join/leave, and data messages. Other room and track callbacks from the C ABI
are not yet exposed. Lua asynchronous calls use a native worker thread and resume coroutines only
when the Lua thread calls `room:poll()` or `room:step()`.
