# Language binding API support

This matrix covers language bindings built on the stable C ABI. The C and C++ APIs themselves are
documented in the [SDK feature guide](../docs/FEATURES.md). `Yes` means the binding exposes the
capability, `Partial` means some related APIs are available, and `No` means it does not. Zig
exposes the complete C ABI as low-level declarations, with owned-handle wrappers for common
room, media, stream, RPC, snapshot, E2EE, and DataTrack operations.
`FFI` in the Rust column means the capability is available through unsafe C ABI
declarations and does not yet have a safe Rust wrapper.
The [Swift binding](swift/README.md) imports the complete C ABI and provides Swift wrappers for
rooms, callbacks, snapshots, media/capture, messaging and streams, RPC, DataTrack, E2EE,
recording, devices, and diagnostics. Native options and specialized controls remain available
through the imported C ABI.
The [Rust binding](rust/README.md) generates the complete C ABI through bindgen and
provides safe SDK/room ownership, connection settings, participant values, messaging,
and bounded callback queues. Media, streams, RPC, E2EE, DataTrack, recording, and
diagnostics are currently exposed through unsafe FFI. It has offline tests and
room/messaging and device-enumeration examples. Windows x64 MSVC is validated;
GNU/Linux and macOS await core SDK support, and no mobile target is declared.
The [Java binding](java/README.md) covers room lifecycle and
callbacks, token sources, participant and publication snapshots, messaging and streams, RPC,
E2EE, DataTrack, local media, physical capture, and bounded remote media readers. The
[.NET binding](dotnet/README.md) covers
room, media, data, stream, RPC, device, recording, and diagnostic operations.

| API capability | [Lua](lua/README.md) | [Python](python/README.md) | [.NET](dotnet/README.md) | [Java](java/README.md) | [Swift](swift/README.md) | [Zig](zig/README.md) | [Rust](rust/README.md) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| SDK version | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| Room creation and cleanup | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| Synchronous room connect and disconnect | Yes | No | Yes | Yes | Yes | Yes | Yes |
| Language-native asynchronous room connect and disconnect | Yes | Yes | Yes | Yes | Yes | No | No |
| Dynamic token source and advanced ICE connection options | Yes | Yes | Yes | Yes | Yes | Yes | FFI |
| Connection state, room SID, name, and metadata getters | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| Connection and participant callbacks | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| Receive data messages | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| Publish data messages synchronously | Yes | No | Yes | Yes | Yes | Yes | Yes |
| Publish data messages asynchronously | Yes | Yes | Yes | Yes | Yes | No | No |
| Local participant and remote participant snapshots | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| Local participant metadata and attributes | Yes | Yes | Yes | Yes | Yes | Yes | Partial |
| Remote track subscription and settings | Yes | Yes | Yes | Yes | Yes | Yes | FFI |
| Track subscription permissions and feedback events | Yes | Yes | Yes | Yes | Yes | Yes | FFI |
| External audio/video tracks, frame push, and remote frame streams | Yes | Yes | Yes | Yes | Yes | Yes | FFI |
| Audio/video frame events with bounded queues and video metadata | Yes | Yes | Yes | Yes | Yes | Yes | FFI |
| Microphone, system audio, camera, and screen device capture | Yes | Yes | Yes | Yes | Yes | Yes | FFI |
| Text and byte streams, chat, and file transfer | Yes | Yes | Yes | Yes | Yes | Yes | Partial |
| DataTrack schema, publishing, and subscription | Yes | Yes | Yes | Yes | Yes | Yes | FFI |
| RPC calls and method handlers | Yes | Yes | Yes | Yes | Yes | Yes | FFI |
| E2EE (C ABI GCM, keys, frame cryptors) | Yes | Yes | Yes | Yes | Yes | Yes | FFI |
| Media devices and remote recording | Yes | Yes | Yes | Yes | Yes | Yes | FFI |
| Logging, tracing, and RTC statistics | Yes | Yes | Yes | Yes | Yes | Yes | FFI |

Lua covers one-shot and incremental outgoing streams, writer progress and completion events,
and incoming completed and incremental stream events.
It also covers DataTrack, RPC, E2EE key control, device enumeration and speaker control, recording
status, and local/remote track RTC snapshots.
Lua exposes remote-track recording, bounded media, logging and tracing record queues, and JSON
trace output. Lua asynchronous calls use a native worker thread and resume coroutines when the Lua
thread calls `room:poll()` or `room:step()`.

## Examples matrix

✅ means a runnable example or smoke test exists; — means there is no corresponding program. A
checkmark does not mean every device or platform has been tested. The columns link to each
binding's example guide (Zig lists examples in its binding guide). Swift includes a room/data
example, a feature example runner, offline lifecycle/callback tests, and an opt-in two-peer test.

| Example area | [Lua](lua/examples/README.md) | [Python](python/examples/README.md) | [.NET](dotnet/examples/README.md) | [Java](java/examples/README.md) | [Swift](swift/Examples/README.md) | [Zig](zig/README.md) | [Rust](rust/README.md) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Room connection and lifecycle | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Device and screen-source enumeration | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | Devices only |
| Data, text, and byte messages | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Incremental stream writer | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| DataTrack schema and frame publishing | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| Dynamic token source | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| Standalone RPC caller and receiver | ✅ | ✅ | ✅ | ✅ | ✅ | — | — |
| E2EE connection | — | — | ✅ | ✅ | ✅ | ✅ | — |
| Synthetic audio and video publishing | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| Remote media readers and frame events | ✅ | ✅ | ✅ | ✅ | ✅ | — | — |
| Remote track recording | ✅ | ✅ | ✅ | ✅ | ✅ | — | — |
| Microphone, system audio, camera, and screen capture | ✅ | ✅ | ✅ | ✅ | ✅ | — | — |
| Local two-participant smoke test | — | ✅ | ✅ | ✅ | ✅ | ✅ | — |

The existing local smoke runners use generated media and temporary credentials. Swift's opt-in
two-peer test reads supplied test credentials and checks data/RPC delivery and snapshot lifetime.
Physical capture examples require real devices and are separate from those smoke tests.

## Platform support matrix

This table follows the [core SDK platform status](../Readme.md#platform-status). ✅ means the
binding can use the currently supported Windows native SDK; ⏳ means the core native SDK port is
planned and the binding has not been validated there; — means no binding or mobile target is
declared. The existence of cross-platform language code or loader instructions does not by itself
establish native SDK support.

| Platform | [Lua](lua/README.md) | [Python](python/README.md) | [.NET](dotnet/README.md) | [Java](java/README.md) | [Swift](swift/README.md) | [Zig](zig/README.md) | [Rust](rust/README.md) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Windows x64 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| GNU/Linux | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ |
| macOS | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ |
| iOS | — | — | — | — | — | — | — |
| Android | — | — | — | — | — | — | — |

The core SDK lists iOS and Android as planned; no language binding in this matrix currently
declares either as a supported target.
