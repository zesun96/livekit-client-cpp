# livekit-client Zig binding

This package uses the stable C ABI in `include/livekit/capi/livekit.h`. It targets
Zig 0.18 development builds, which generate the C declarations with `zig
translate-c` during `zig build`. `@import("livekit").c` exposes the complete C
API, including media, DataTrack, streams, RPC, E2EE, recording, diagnostics,
and all callbacks. The Zig layer adds ownership and error handling for room,
snapshots, external media sources and tracks, stream writers, RPC results, and
local DataTracks. Less common calls retain their native signatures under `c`.

The binding links the shared `livekitclient` library. Build the shared C++ SDK
first, then point Zig at its import library directory:

```powershell
cd pkg/zig
$env:Path = 'C:\path\to\livekitclient\bin;' + $env:Path
zig build test -Dnative-lib-dir=C:/path/to/livekitclient/lib
zig build example -Dnative-lib-dir=C:/path/to/livekitclient/lib
zig build integration-build -Dnative-lib-dir=C:/path/to/livekitclient/lib
```

The DLL and its native dependencies must be on the loader path to run tests or
the example. The header and native library must come from the same SDK build;
older installed binaries can lack symbols declared by a newer source header.
On Linux/macOS, use the directory containing the shared library and configure
the platform library loader path.

`zig build example` installs these executables into `zig-out/bin`:

| Example | Behavior | Physical device access |
| --- | --- | --- |
| `livekit_room_zig` | Connect, print local identity, publish data, disconnect | No |
| `livekit_data_transfer_zig` | Send data, text, bytes, and chat | No |
| `livekit_stream_writer_zig` | Write an incremental text stream | No |
| `livekit_data_track_schema_zig` | Store a JSON schema and publish a DataTrack frame | No |
| `livekit_publish_audio_zig` | Publish one second of generated PCM | No |
| `livekit_publish_video_zig` | Publish one second of generated I420 frames | No |
| `livekit_token_source_zig` | Connect through a credential callback | No |
| `livekit_e2ee_zig` | Connect with E2EE and set a shared key | No |
| `livekit_media_devices_zig` | List device and screen source IDs | Enumerates only |

The connection examples read `LIVEKIT_URL` and `LIVEKIT_TOKEN` from the
environment. Run them with two different participant tokens in the same room
to receive their messages. `livekit_media_devices_zig` needs no server or
credentials.
The E2EE example also requires `LIVEKIT_E2EE_KEY` with at least 32 bytes;
supply a fresh secret in a real application. DataTrack needs a server with
`enable_participant_data_blob: true`.

`zig build test` runs offline native tests for room lifecycle, snapshots,
disconnected operation errors, C option defaults, E2EE state, external media
source creation, string getters, and device enumeration. They do not open
physical media devices or require a server.

The optional `tests/two_peer_smoke.zig` connects two peers and verifies
participant, data, chat, text-stream, and byte-stream callbacks plus an RPC round trip.
To run it against an existing server, set
`LIVEKIT_URL`, `LIVEKIT_TOKEN_A`, and `LIVEKIT_TOKEN_B` for two distinct
identities in the same room, then run `zig build integration`.

On Windows, the following script uses the checked-in local LiveKit Server and
CLI, creates temporary test-only credentials, runs the two-peer smoke and the
connection, stream, DataTrack, generated media, token-source, and E2EE
examples, then stops the server and removes its temporary files:

```powershell
zig build test example integration-build -Dnative-lib-dir=C:/path/to/livekitclient/lib
& tests/run_local_smoke.ps1 -BuildDirectory out/build/sdk-dll-md-release/Release
```

## Usage

Import the `livekit` module configured in this package's `build.zig`. The main
types are:

- `livekit.c`: exact C ABI declarations and constants.
- `livekit.Room`: owned room handle with `deinit`, connection, state, identity,
  snapshot, media-track creation, data publish, stream, RPC, DataTrack, and E2EE
  methods. Call `waitConnected(io, timeout_ms)` after `connect` before operations
  that require an established room.
- `livekit.AudioSource`, `VideoSource`, `LocalTrack`, `TextWriter`, `ByteWriter`,
  `DataTrack`, `RpcResult`, `LocalSnapshot`, and `RemoteParticipants`: owned
  native handles with matching `deinit` methods.
- `livekit.*Options()` helpers: versioned native structures initialized with
  the required C ABI functions. Other option structures use `c.lk_*_init`.
- `livekit.check(status)` and `lastError(allocator)`: status conversion and
  immediate copy of the native error message.
- `livekit.readString(allocator, getter, args)`: reads any C ABI string getter;
  the caller owns and must free the returned slice.

Call `livekit.init()` before creating native objects and `livekit.shutdown()`
after destroying them. Each owned C handle must be destroyed exactly once.
Destroy tracks before sources; keep published tracks alive until the room is
disconnected. Never destroy a room from one of its callbacks. Callback fields
are borrowed and valid only for the duration of the call. Callbacks can arrive
from SDK threads, so synchronize shared Zig state and keep `user_data` alive
until the room is disconnected or destroyed. Use `c.lk_*_init` for every
versioned options or output structure before passing it to the SDK.

Room calls are synchronous. For asynchronous application flows, run blocking
calls on your own worker thread. The binding does not provide a coroutine
scheduler or queue callbacks onto an application event loop.
