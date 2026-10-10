# livekit-client Lua binding

This Lua 5.1+ binding uses the stable C ABI of `livekit-client-cpp`. It exposes room lifecycle,
participant snapshots and updates, data and stream messages, RPC calls, E2EE keys, media capture,
logging and tracing configuration, and remote-track controls. The Lua interpreter and module must
use the same Lua runtime DLL and architecture. The `Lua51vs_rel_pdb` executable is statically
linked and cannot safely host this
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
Connection options support `auto_subscribe`, `adaptive_stream`, `dynacast`, `join_retries`,
`reconnect_timeout_ms`, `continual_gathering_policy`, `ice_transport_type`, `ice_servers`,
`reconnect_policy`, and
an `e2ee` table. ICE servers are arrays of `{urls = {"stun:..."}, username, password}`;
`CONTINUAL_GATHERING_POLICY` and `ICE_TRANSPORT_TYPE` provide enum values.
`reconnect_policy(context)` receives `retry_count`, `elapsed_ms`, `reason`, and `server_url`.
Return a nonnegative integer delay in milliseconds to retry, or `nil`/`false` to stop.
The callback runs on the Lua thread during `poll()`/`step()` and must not yield. Keep polling
during recovery; an unanswered policy request stops retrying after 30 seconds.
For renewable credentials, use `room:connect_with_token_source(provider, fetch_options,
connect_options)` or its `_async` variant. The provider receives room and participant fields,
`participant_attributes`, and `force_refresh`; return `{url = ..., token = ...}` or two strings.
Fetch options can include room, participant, agent, and deployment fields. The provider runs on
the Lua thread while `poll()` or `step()` handles a native request. Continue driving the room
event loop during reconnection so refreshed credentials can be supplied. The synchronous
variant drives the loop for the initial connection.
The E2EE table accepts `enabled`, binary `shared_key`, `ratchet_salt`,
`unencrypted_magic_bytes`, `ratchet_window_size`, `failure_tolerance`, `key_ring_size`, and
`key_derivation` (`0` for PBKDF2 SHA-256, `1` for HKDF SHA-256).
Fallible methods return `true` or `nil, message`. Data payloads are binary-safe Lua strings.
For recipient targeting, use `publish_data_with_options(payload, {reliable, topic,
destination_identities})`. One-shot streams have `send_text_with_options(text, options)`,
`send_bytes_with_options(data, options)`, and `send_file_with_options(path, options)`.
These accept `topic`, `destination_identities`, `attributes`, `chunk_size`, and `compress`;
text also accepts `reply_to_stream_id` and `attached_stream_ids`, while bytes and file accept
`mime_type` and bytes accept `name`. All four methods have `*_async` variants.

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

`livekit.log_options()` returns process-wide `livekit_level`, `webrtc_level`, and
`websocket_level` values. `livekit.set_log_options({...})` updates only the supplied levels;
constants `LOG_TRACE`, `LOG_DEBUG`, `LOG_INFO`, `LOG_WARNING`, `LOG_ERROR`, and `LOG_OFF`
are provided. `livekit.trace_options()` and `livekit.set_trace_options({...})` read and update
`enabled` and `category_mask`. Combine `TRACE_LIFECYCLE`, `TRACE_SIGNALING`,
`TRACE_TRANSPORT`, `TRACE_TRACK`, `TRACE_DATA`, `TRACE_RPC`, and `TRACE_E2EE` by addition, or use
`TRACE_ALL`. `livekit.trace_start_json_file(path)` replaces the process-wide trace sink with a
Chrome Trace JSON file; call `livekit.trace_stop()` to flush and close it. Use
`livekit.log_capture_start()` and `livekit.read_log_records()` to collect log records, then
`livekit.log_capture_stop()` to unregister the sink. Each log record has `level`, `source`,
`message`, `file`, and `line`. Use `livekit.trace_capture_start()` and
`livekit.read_trace_records()` to collect trace records, then `livekit.trace_stop()` to unregister
the sink. Trace records contain `phase`, `category`, `name`, `timestamp_us`, `thread_id`, and
`correlation_id`; the two IDs are decimal strings to preserve all 64 bits in Lua 5.1.
Each read returns `records, dropped_count` and drains a bounded queue of 1024 records.
These sinks and settings affect all rooms in the process; starting a capture replaces any
previous sink of the same kind, and starting JSON tracing replaces trace capture.
`livekit.last_error_info()` returns the current thread's `{domain, code, message}` snapshot;
`livekit.ERROR_DOMAIN` names the domain values.

`room:local_participant()` and `room:remote_participants()` return detached tables. Local
and remote participant tables include `permissions`. Remote publication tables include
`is_simulcasted`, `encryption`, and an optional `subscription_error`; subscribed publications
also include a `track` table with its stream state, dimensions, and enabled flag.
The local participant's name, metadata, and attributes can be changed through `set_local_name`,
`set_local_metadata`, and `set_local_attributes`. Remote tracks can be controlled with
`set_remote_track_subscribed(participant_sid, track_sid, subscribed)` and
`update_remote_track_settings(participant_sid, track_sid, settings)`.
Use `set_track_subscription_permissions(all_allowed, permissions)` to restrict access to local
tracks. Each permission table may specify `participant_identity` or `participant_sid`, `allow_all`,
and an `allowed_track_sids` array. Pass an empty array to clear per-participant permissions.

`republish_all_tracks()` and `republish_all_tracks_async()` republish local media tracks after
a connection is established.

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

For incremental transfer, call `room:stream_text(options)` or `room:stream_bytes(options)` to
obtain a room-owned writer ID. Both have `*_async` variants. Use
`stream_writer_write(id, data)` for each chunk, then `stream_writer_close(id)` or
`stream_writer_cancel(id, reason)`. Those three operations also have `*_async` variants.
`stream_writer_info(id)` returns the stream ID, topic, MIME type, attributes, and close state;
`stream_writer_release(id)` destroys the handle after it is no longer needed. Room close releases
any remaining writers. Options accept `topic`, `stream_id`, `destination_identities`,
`attributes`, `total_size`, `chunk_size`, and `compress`. Text writers also accept
`reply_to_stream_id`, `attached_stream_ids`, `update`, and `version`; byte writers accept
`mime_type` and `name`. Stream data is delivered through `text_received` and `byte_received`
events after completion. Lua strings preserve binary byte chunks. Completed text, byte, and file
events include `attributes`; completed text events also include `attached_stream_ids`.
For chunk-by-chunk receive, call `register_text_stream_handler(topic)` or
`register_byte_stream_handler(topic)` and handle `text_stream_event` or `byte_stream_event` in
`room:on`. The event carries `phase` (0 open, 1 chunk, 2 closed, 3 failed), binary `content`,
`chunk_index`, stream metadata, and a failure `reason`. Registering a topic handler replaces the
completed-message event for that topic until `unregister_*_stream_handler(topic)` is called.

DataTrack uses binary-safe Lua strings for schema definitions and frames. Call
`store_data_track_schema({name, encoding, custom_encoding}, definition)` before publishing a
track that names that schema. `get_data_track_schema(participant_identity, schema_id)` retrieves
its definition. These operations have `*_async` variants. `publish_data_track(name,
{frame_encoding, custom_frame_encoding, schema})`
returns a room-owned ID; `data_track_info(id)`, `data_track_push(id, bytes, user_timestamp)`, and
`unpublish_data_track(id)` manage it. Publishing also has an `*_async` variant.
`remote_data_tracks()` returns detached snapshots with
participant identity and track SID. Subscribe with
`subscribe_data_track(identity, sid, {target_fps, buffer_capacity, max_partial_frames})`, then
call `read_data_track_frame(reader_id, timeout_ms)` to get `{data, user_timestamp}`. A zero
timeout polls without waiting. Subscription also has an `*_async` variant.
`data_track_reader_stats(id)` and `close_data_track_reader(id)`
manage the reader. `update_data_track_subscription(identity, sid, options)` changes its options.
Synchronous DataTrack failures return `nil, message, error_code`; async futures return
`nil, message`. The server must enable
`enable_participant_data_blob` for schema storage and lookup.

`e2ee_is_configured`, `e2ee_is_enabled`, `e2ee_set_enabled`, shared and participant key methods,
`e2ee_data_key_index`, `e2ee_set_data_key_index`, and frame-cryptor methods expose E2EE control.
Keys are binary-safe Lua strings; exporting them returns the raw key bytes. Use
`e2ee_frame_cryptors()` to inspect current cryptors. The binding also exposes
`list_media_devices()`, speaker controls, audio playback statistics, recording status, and
message, participant, track, and encryption state events through `room:on`.
Additional `room:on` events include `local_track_subscribed`,
`participant_permissions_changed`, `transcription_received`,
`subscribed_quality_update`, and `metrics_received`. Permissions events carry
`previous_permissions` and `permissions` tables with publication and subscription flags plus
`can_publish_sources`. Transcription events
carry `identity`, `sid`, and `segments` with text, language, times, and finality. Quality updates
carry track `sid`, `qualities`, and per-codec `codecs` arrays.
Metrics events carry `timestamp_ms`, optional `normalized_timestamp`, `string_data`,
`time_series`, and `events`. Metric label and participant/track/rid fields are zero-based indices
into `string_data`; add one when indexing the Lua array.

## Media publishing and subscription

`publish_audio_track(label, sample_rate, channels, queue_ms)` creates and publishes an external
audio source. It returns a room-owned track ID. `push_audio_frame(track_id, pcm)` accepts exactly
10 ms of interleaved signed 16-bit PCM as a binary Lua string. The defaults are 48 kHz, mono, and
a 200 ms source queue. The sample rate must be divisible by 100, and the queue size must be a
multiple of 10 ms. An optional fifth `publish_options` table configures the audio publication.
`publish_video_track(label, first_frame, width, height, format, screen, publish_options)`
creates and publishes an external video source; a first frame is required before publishing.
`push_video_frame(track_id, pixels, width, height, format, timestamp_us, metadata)` submits later
frames. Optional `metadata` accepts `user_timestamp_us`, `frame_id`, binary `user_data`, and
`rotation` (0, 90, 180, or 270 degrees). Enable the corresponding
`publish_options.frame_metadata_features` fields before publishing to request metadata transport.
`format` is `"RGBA"` (default) or `"I420"`; I420 frames require even dimensions. Use
`set_local_track_muted(track_id, muted)` and `unpublish_local_track(track_id)` to control and
release a publication. Publishing and unpublishing also have `*_async` variants that return
coroutine futures; frame push remains a direct call on the Lua thread.
The same publication options work with both async methods. Capture track options accept a nested
`publish_options` table. Publication options include `dtx`, `red`, `simulcast`, `stream`,
`video_codec`, `scalability_mode`, `backup_video_codec`, `backup_codec_policy`,
`video_encoding`, `backup_video_encoding`, `frame_metadata_features`, `preconnect_buffer`, and
`degradation_preference`. Encoding tables accept `max_bitrate` and `max_framerate`; metadata
feature tables accept `user_timestamp`, `frame_id`, and `user_data` booleans. Use
`VIDEO_CODEC`, `BACKUP_CODEC_POLICY`, and `VIDEO_DEGRADATION_PREFERENCE` for enum values.

Audio source queue controls are `audio_source_queued_duration_ms(track_id)`,
`clear_audio_source_queue(track_id)`, and `wait_audio_source_playout(track_id, timeout_ms)`;
the wait also has an `*_async` variant. Published video tracks support
`update_video_encoding(track_id, {max_bitrate, max_framerate}, backup_codec)` and
`update_video_degradation_preference(track_id, preference)`. Zero bitrate or frame rate restores
the source-derived default. Use values from `VIDEO_DEGRADATION_PREFERENCE`.

For explicit remote subscription, connect with `{auto_subscribe = false}` and call
`set_remote_track_subscribed(participant_sid, track_sid, true)` after `track_published`. The track
event contains both `participant_sid` and `participant_identity`. Use
`set_remote_track_subscribed_async(...)` for a coroutine future. After `track_subscribed`, use
`open_audio_stream(participant_identity, track_sid, capacity)` or `open_video_stream(...)` to get a
room-owned stream ID. `read_audio_frame(stream_id, timeout_ms)` returns a table with binary PCM,
sample rate, channel count, and samples per channel. `read_video_frame(...)` returns decoded I420
bytes, dimensions, timestamp, and a `metadata` table. The metadata table contains optional
`user_timestamp_us`, `frame_id`, and binary `user_data` fields. The read methods return
`nil, "empty"` when no frame is ready
and `nil, "closed"` after the stream ends. A zero timeout (default) is nonblocking. Use
`close_remote_stream(stream_id)` to release the reader; `remote_stream_is_closed` and
`remote_stream_dropped_frames` report its state. All track and stream IDs expire when the room
closes. Media sources and readers are released automatically on room close.

`local_track_rtc_stats(track_id)` and `remote_track_rtc_stats(participant_identity, track_sid)`
return arrays of RTC stream statistics. A report that is not ready returns an empty array.
Optional measurements such as bitrate, jitter, round-trip time, and audio level are omitted when
unavailable. Large counters are Lua numbers and may lose integer precision above 2^53.

## Device capture

`publish_microphone_track(label, options)`, `publish_system_audio_track(label, options)`,
`publish_camera_track(label, options)`, and `publish_screen_track(label, options)` open a capture
source and publish it. Each returns a track ID compatible with `set_local_track_muted`,
`local_track_rtc_stats`, and `unpublish_local_track`. All four also have `*_async` variants so
opening a device can run on the room worker thread. Capture begins when the source is created.
Unpublishing or closing the room releases the source. `start_capture`, `stop_capture`, and
`switch_capture_source` control the source afterward; each also has a coroutine `*_async`
variant. `capture_is_running(track_id)` and `capture_source_id(track_id)` report its state.

Microphone and system audio options accept `device_id` (empty for system default) and `queue_ms`
(default 200, a positive multiple of 10). Microphone options also accept `echo_cancellation`,
`auto_gain_control`, and `noise_suppression` booleans, all true by default. Camera options accept
`device_id`, `width` (default 1280), `height` (default 720), and `fps` (default 30). Screen options
require a `source_id` from `livekit.list_screen_sources()` and accept `fps` (default 15) and
`include_cursor` (default true). Use `livekit.list_media_devices()` for microphone, speaker, and
camera IDs. Screen source `kind` is `0` for a monitor or `1` for a window.
`microphone_is_muted`, `microphone_set_muted`, `microphone_volume`,
`microphone_set_volume`, `microphone_processing_options`,
`microphone_set_processing_options`, and `microphone_processing_stats` expose microphone source
controls and measurements. The source mute is distinct from `set_local_track_muted`.

Additional room events include `room_sid_changed` (`previous_sid`, `sid`),
`connection_quality_changed` (`identity`, `quality`), and `active_speakers_changed`
(`identities`, an array of participant identities). Track mute changes arrive as `track_muted`
and `track_unmuted` with track and participant fields.
`room_updated` and `room_moved` carry room SID, name, metadata, and recording state.
`token_refreshed`, `room_eos`, and `participants_updated` (an `identities` array) report
connection lifecycle updates. `sip_dtmf_received` carries `digit`, `code`, and `identity`;
`data_channel_buffer_status_changed` carries buffered amount and water marks.
DataTrack events are `data_track_published`, `data_track_unpublished`,
`local_data_track_published`, `local_data_track_unpublished`, and `data_track_frame`.
The frame event includes binary `data` and optional `user_timestamp`.
Subscription feedback arrives as `track_subscription_permission_changed` (`allowed`),
`track_subscription_failed` (`error`), `track_stream_state_changed` (`state`), and
`track_subscription_status_changed` (`status`); each includes the track `sid` and participant
`identity` when available.

Runnable examples are in [examples](examples/README.md).

The binding initializes the LiveKit runtime when loaded. The runtime remains active until process
exit; do not call `lk_shutdown()` externally while Lua rooms may still exist. Audio/video frame
callbacks and some advanced C API features are not yet wrapped.
