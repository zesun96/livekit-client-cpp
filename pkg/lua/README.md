# livekit-client Lua binding

This Lua 5.1+ binding uses the stable C ABI of `livekit-client-cpp`. It exposes room lifecycle,
participant snapshots and updates, data and stream messages, RPC calls, E2EE keys, and device and
remote-track controls. The Lua interpreter and module must use the same Lua runtime DLL and
architecture. The `Lua51vs_rel_pdb` executable is statically linked and cannot safely host this
DLL-linked module; use a DLL-linked interpreter or the optional test runner below.

## Build on Windows

Install or build the shared LiveKitClient package, then configure this directory separately:

```powershell
cmake -S pkg/lua -B out/build/lua -G "Visual Studio 17 2022" -A x64 `
  -DLUA_INCLUDE_DIR=C:/path/to/lua/include `
  -DLUA_LIBRARY=C:/path/to/lua/lib/lua.lib `
  -DLiveKitClient_DIR=C:/path/to/livekit/lib/cmake/LiveKitClient
cmake --build out/build/lua --config Release
```

For the local `E:/workspace/Lua/Lua51vs_rel_pdb` build, set `LUA_LIBRARY` to
`E:/workspace/Lua/Lua51vs_rel_pdb/lib/lua51.lib`. Add `-DLKC_LUA_BUILD_TEST_RUNNER=ON` to build
`livekit_lua_test_runner.exe`, which links the same `lua51.dll` as the module. Run
`livekit_lua_test_runner.exe pkg/lua/tests/smoke.lua` after setting `LUA_PATH` and `LUA_CPATH`.

Place `livekit_client_native.dll`, `livekitclient.dll`, `websockets.dll`, and the matching Lua runtime DLL
where the Windows loader can find them. Put `pkg/lua/lua` on `LUA_PATH`, and the directory containing
`livekit_client_native.dll` on `LUA_CPATH` (or configure `package.path` and `package.cpath` in Lua).
The public entry point is `require("livekit-client")`. `require("livekit")` remains an alias for
existing callers.

## Synchronous calls

```lua
local livekit = require("livekit-client")
local room, err = livekit.new_room()
assert(room, err)

room:on(function(event)
  if event.type == "data_received" then
    print(event.identity, event.topic, event.data)
  elseif event.type == "disconnected" then
    print("disconnected", event.reason)
  end
end)

assert(room:connect(os.getenv("LIVEKIT_URL"), os.getenv("LIVEKIT_TOKEN")))
while room:is_connected() do
  local count, callback_error = room:step(100)
  assert(count, callback_error)
end
room:close()
```

`room:connect(url, token, options)`, `room:disconnect()`, and
`room:publish_data(payload, reliable, topic)` are blocking calls. `reliable` defaults to `true`.
Connection options support `auto_subscribe`, `adaptive_stream`, `dynacast`, and an `e2ee` table.
The E2EE table accepts `enabled`, binary `shared_key`, `ratchet_salt`,
`unencrypted_magic_bytes`, `ratchet_window_size`, `failure_tolerance`, `key_ring_size`, and
`key_derivation` (`0` for PBKDF2 SHA-256, `1` for HKDF SHA-256).
Fallible methods return `true` or `nil, message`. Data payloads are binary-safe Lua strings.

## Coroutine asynchronous calls

Connect, disconnect, data publish, chat, text, bytes, file send, and outgoing RPC have
`*_async` variants. Each returns a future or `nil, message`. The asynchronous chat call returns
`{id, timestamp}` on success.
Futures support `:result()` (nonblocking),
`:wait()` (blocking), and `:await()` (yields the current Lua coroutine). Use `livekit.spawn` to
register a coroutine, then call `room:step()` in the host event loop:

```lua
local livekit = require("livekit-client")
local room = assert(livekit.new_room())
local task = livekit.spawn(function()
  local ok, err = room:connect_async(url, token):await()
  assert(ok, err)
  assert(room:publish_data_async("hello", true, "greeting"):await())
  return room:disconnect_async():await()
end)

while not task:done() do
  local count, err = room:step(50)
  assert(count, err)
end
local finished, ok, err = task:result()
assert(finished and ok, err)
room:close()
```

`livekit.run(room, function() ... end)` drives one room's coroutine until it finishes and returns
its values. It blocks the caller while running the event loop. For an existing event loop, call
`room:poll(max_events)` to dispatch events and resume ready coroutines without waiting, or
`room:step(timeout_ms, max_events)` to wait for an event or async result first. `timeout_ms` is a
single poll interval, not an operation deadline. To avoid blocking the Lua thread, use `:await()`
inside a `livekit.spawn` coroutine and keep calling `poll` or `step` from the Lua thread.

Native callbacks only copy data into a queue; they never call Lua from SDK threads. The event queue
holds up to 1024 events and drops the oldest when full; `room:dropped_events()` reports that count.
An event handler error returns `nil, message` from `poll` or `step`. Async operations on a room run
in order on one worker thread. A synchronous operation returns `nil, message` while async work is
pending or running. `room:close()` waits for an operation already in progress, discards queued
operations, and is idempotent. The room is also closed by Lua garbage collection; explicitly close
rooms before the Lua state exits.

## Additional API

`room:local_participant()` and `room:remote_participants()` return detached tables. Local
participant name, metadata, and attributes can be changed through `set_local_name`,
`set_local_metadata`, and `set_local_attributes`. Remote tracks can be controlled with
`set_remote_track_subscribed(participant_sid, track_sid, subscribed)` and
`update_remote_track_settings(participant_sid, track_sid, settings)`.
Use `set_track_subscription_permissions(all_allowed, permissions)` to restrict access to local
tracks. Each permission table may specify `participant_identity` or `participant_sid`, `allow_all`,
and an `allowed_track_sids` array. Pass an empty array to clear per-participant permissions.

`edit_chat_message` and `publish_dtmf` are synchronous. `perform_rpc(destination, method,
payload, timeout_ms)` and its
async variant return a table with `ok`, `payload`, `error_code`, `error_message`, and `error_data`.
An RPC response error sets `ok = false`; C API call failures return `nil, message`.
Use `register_rpc_method(method, handler)` to receive RPC calls and
`unregister_rpc_method(method)` to remove a handler. The handler receives a table containing
`request_id`, `caller_identity`, `payload`, and `response_timeout_ms`. It may return a response
string or a table with `payload`, `error_code`, `error_message`, and `error_data`. A handler error
becomes an application RPC error. Inbound handlers run on the Lua thread during `poll()` or
`step()`, so the host must keep driving that loop while receiving calls. A handler must return
promptly and cannot yield; avoid blocking room calls inside it.

`e2ee_is_configured`, `e2ee_is_enabled`, `e2ee_set_enabled`, shared and participant key methods,
`e2ee_data_key_index`, `e2ee_set_data_key_index`, and frame-cryptor methods expose E2EE control.
Keys are binary-safe Lua strings; exporting them returns the raw key bytes. Use
`e2ee_frame_cryptors()` to inspect current cryptors. The binding also exposes
`list_media_devices()`, speaker controls, audio playback statistics, recording status, and
message, participant, track, and encryption state events through `room:on`.

## Media publishing and subscription

`publish_audio_track(label, sample_rate, channels, queue_ms)` creates and publishes an external
audio source. It returns a room-owned track ID. `push_audio_frame(track_id, pcm)` accepts exactly
10 ms of interleaved signed 16-bit PCM as a binary Lua string. The defaults are 48 kHz, mono, and
a 200 ms source queue. The sample rate must be divisible by 100, and the queue size must be a
multiple of 10 ms. `publish_video_track(label, first_frame, width, height, format, screen)`
creates and publishes an external video source; a first frame is required before publishing.
`push_video_frame(track_id, pixels, width, height, format, timestamp_us)` submits later frames.
`format` is `"RGBA"` (default) or `"I420"`; I420 frames require even dimensions. Use
`set_local_track_muted(track_id, muted)` and `unpublish_local_track(track_id)` to control and
release a publication. Publishing and unpublishing also have `*_async` variants that return
coroutine futures; frame push remains a direct call on the Lua thread.

For explicit remote subscription, connect with `{auto_subscribe = false}` and call
`set_remote_track_subscribed(participant_sid, track_sid, true)` after `track_published`. The track
event contains both `participant_sid` and `participant_identity`. Use
`set_remote_track_subscribed_async(...)` for a coroutine future. After `track_subscribed`, use
`open_audio_stream(participant_identity, track_sid, capacity)` or `open_video_stream(...)` to get a
room-owned stream ID. `read_audio_frame(stream_id, timeout_ms)` returns a table with binary PCM,
sample rate, channel count, and samples per channel. `read_video_frame(...)` returns decoded I420
bytes, dimensions, and timestamp. The read methods return `nil, "empty"` when no frame is ready
and `nil, "closed"` after the stream ends. A zero timeout (default) is nonblocking. Use
`close_remote_stream(stream_id)` to release the reader; `remote_stream_is_closed` and
`remote_stream_dropped_frames` report its state. All track and stream IDs expire when the room
closes. Media sources and readers are released automatically on room close.

`local_track_rtc_stats(track_id)` and `remote_track_rtc_stats(participant_identity, track_sid)`
return arrays of RTC stream statistics. A report that is not ready returns an empty array.
Optional measurements such as bitrate, jitter, round-trip time, and audio level are omitted when
unavailable. Large counters are Lua numbers and may lose integer precision above 2^53.

Additional room events include `room_sid_changed` (`previous_sid`, `sid`),
`connection_quality_changed` (`identity`, `quality`), and `active_speakers_changed`
(`identities`, an array of participant identities). Track mute changes arrive as `track_muted`
and `track_unmuted` with track and participant fields.
Subscription feedback arrives as `track_subscription_permission_changed` (`allowed`),
`track_subscription_failed` (`error`), `track_stream_state_changed` (`state`), and
`track_subscription_status_changed` (`status`); each includes the track `sid` and participant
`identity` when available.

Runnable examples are in [examples](examples/README.md).

The binding initializes the LiveKit runtime when loaded. The runtime remains active until process
exit; do not call `lk_shutdown()` externally while Lua rooms may still exist. Device capture,
audio/video frame callbacks, and some advanced C API features are not yet wrapped.
