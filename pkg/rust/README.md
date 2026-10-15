# Rust binding

Rust bindings for the LiveKit C++ client stable C ABI. The `ffi` module is generated
from `include/livekit/capi/livekit.h` during each build and exposes the complete ABI,
including media/capture, frame readers, streams, RPC, E2EE, DataTrack, recording,
devices, logging, tracing, and statistics.

The safe API covers SDK/room ownership, ICE and token sources, participant and
track snapshots, metadata/attributes, data/chat/file messaging, incremental text
and byte streams, RPC calls and handlers, E2EE keys and frame cryptors, DataTrack
schemas/publishing/readers, external media, physical capture, bounded remote media
readers, recording, devices, and logging/tracing/statistics. Specialized native
options and callbacks remain available through `unsafe` FFI. There is no Rust async
runtime dependency or language-native async API.

## Requirements and build

- Rust 1.77+ and Cargo (validated with Rust 1.87 on Windows x64 MSVC).
- A matching native SDK shared build, its C headers, and `livekitclient.lib` on
  Windows. Reuse the repository's existing SDK build or extracted SDK package.
- libclang, used by the pinned `bindgen` build dependency. Set `LIBCLANG_PATH` to
  the directory containing `libclang.dll` if it is not found automatically. Its
  dependent DLLs must also be discoverable through `PATH`.
- The native DLL and its runtime dependencies (including `websockets.dll` for
  builds using it) available through `PATH` when running examples or tests.

From this directory in PowerShell, using an existing repository SDK build:

```powershell
$native = (Resolve-Path ../../out/build/sdk-dll-md-release/Release).Path
$env:LIVEKIT_CLIENT_CPP_LIB_DIR = $native
$env:Path = "$native;$env:Path"
# If necessary, set LIBCLANG_PATH and add its dependency directory to Path.
cargo test --locked --all-targets
cargo build --locked --examples
cargo run --locked --example media_devices
```

`LIVEKIT_CLIENT_CPP_LIB_DIR` controls the linker search path, not DLL loading.
`LIVEKIT_CLIENT_CPP_INCLUDE_DIR` optionally selects a native SDK `include` directory;
otherwise the build uses `../../include` relative to this crate. Keep headers,
import library and native DLL from the same SDK version. Cargo does not download
or build the native SDK. The crate uses a dynamic native library and is currently
consumed from this repository, rather than published on crates.io.

Add it to an application's `Cargo.toml` with a path dependency:

```toml
[dependencies]
livekit-client-cpp = { path = "path/to/livekit-client-cpp/pkg/rust" }
```

The crate's import name is `livekit_client_cpp`:

```rust,no_run
use livekit_client_cpp::Sdk;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let sdk = Sdk::new()?;
    let mut room = sdk.room()?;
    room.connect(&std::env::var("LIVEKIT_URL")?, &std::env::var("LIVEKIT_TOKEN")?)?;
    room.publish_data(b"hello\0Rust", "example", true)?;
    room.disconnect()?;
    Ok(())
}
```

## Ownership and callbacks

`Sdk` manages process initialization with a synchronized reference count. Each
room, source, stream, frame, writer, recorder, and diagnostic guard retains an SDK
owner; dropping the last owner shuts down the native SDK.
Do not manually call `ffi::lk_init` or `ffi::lk_shutdown` alongside this lifecycle.

`Room` is neither `Send` nor `Sync`. Create, use, and destroy it on its owning
thread. Synchronous operations can block that thread. Room destruction waits for
native callbacks before freeing their storage. Participant snapshots return owned
Rust strings that remain valid after room destruction. Native failures copy the
thread-local error immediately into an owned `Error`.

Room event callbacks copy their payloads and enqueue events without calling application code.
Call `room.try_recv()` to drain them. The default queue capacity is 256; use
`sdk.room_with_event_capacity(n)` to customize it. Full queues discard new events,
and `room.dropped_events()` reports the cumulative count. Individual payload sizes
are not capped. Strings use lossy UTF-8 decoding; binary data retains embedded NULs.

The room retains created media tracks and published DataTracks through native room
destruction, even if the application drops its track handle. Media tracks retain
their sources. This enforces disconnect-before-track-destruction and
track-before-source-destruction ordering. Tracks remain retained even after
unpublishing; destroy the room to release its retained handles. A caller-held track
or source can outlive the room, and operations that require a live room return an
error. Cross-room publishing is rejected.

RPC handlers and token providers are the exceptions to queued event dispatch:
they run synchronously on native SDK threads, must be `Send + Sync + 'static`, and
must return quickly. Their contexts stay alive through room destruction (including
unregistered RPC contexts and failed token-source connections). Panics and invalid
NUL-containing responses become native failures. Callback return strings remain
in thread-local storage until C++ has copied them. `RpcResponse::Failure` is a
remote/application failure; `Err(Error)` reports a native call failure.

Pull readers use `Read::Frame`, `Read::Empty`, and `Read::Closed`, so timeout and
terminal closure remain distinct. Owned media frames expose borrowed slices tied
to the frame lifetime. `close()` wakes readers; dropping handles releases native
resources. `Recorder::stop()` must complete before reading its output file.

Logging and tracing use process-wide sinks. Only one Rust log sink and one Rust
trace sink (callback or JSON file) may be active at a time. Their guards unregister
and wait for callbacks on drop. Do not replace those sinks manually through FFI
while their Rust guards are active.

`Room::as_raw()` borrows the native handle for advanced unsafe operations. Do not
destroy it or replace its callback table. For resources created through `ffi`, obey
the C header's ownership rules: destroy tracks before sources, keep published tracks
alive until disconnect, destroy native handles before their SDK owner, and never
destroy a room inside its callback. Raw callbacks must keep their context alive,
copy borrowed data if retained, and prevent Rust panics from crossing the ABI.

## Examples and validation

| Command | Behavior |
| --- | --- |
| `cargo test --locked --all-targets` | Offline lifecycle, external media, snapshot lifetime, native errors, RPC callback safety, ICE/E2EE option lifetimes, diagnostic guards, callback copying/overflow, string growth, and generated ABI layout tests; compile examples and the ignored two-peer test |
| `cargo run --locked --example media_devices` | Enumerate devices and screen sources without opening physical media devices |
| `cargo run --locked --example room` | Connect using `LIVEKIT_URL`/`LIVEKIT_TOKEN`, send data/text, poll events, and disconnect |
| `cargo run --locked --example features -- diagnostics` | Run offline log/trace queue example |
| `cargo run --locked --example features -- streams` | Send and receive incremental text/byte streams |
| `cargo run --locked --example features -- publish_media` | Publish generated PCM and I420 frames |

See [the examples guide](examples/README.md) for RPC, E2EE, token source, DataTrack,
remote readers/recording, and physical capture scenarios, and the opt-in two-peer test.

The room example requires an explicitly selected test server and token. Offline tests
do not connect to a server or open physical audio/video devices. Live server behavior
is not validated by the offline tests. The two-peer test is ignored by default and
additionally requires `BUILD_INTEGRATION_TESTS=ON` and supplied test credentials.

Windows x64 MSVC is the validated platform. GNU/Linux and macOS remain subject to
the core SDK port and have not been validated; iOS and Android are not declared
binding targets.
