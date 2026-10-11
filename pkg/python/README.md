# livekit-client Python binding

This Python 3.10+ package uses the shared `livekit-client-cpp` C ABI. Its public
import is `from livekit import rtc`, matching the official Python RTC SDK where
the underlying C++ SDK supports the same behavior. It requires the native
`livekitclient` shared library; no Python extension compilation is needed.

```powershell
python -m pip install -e pkg/python
$env:LIVEKIT_CLIENT_CPP_LIBRARY = 'C:\path\to\livekitclient.dll'
```

The Windows loader must also find `websockets.dll` and the other native
dependencies. Set `LIVEKIT_CLIENT_CPP_LIBRARY` to the full path of the SDK DLL,
or put the SDK library on the platform's library search path.

```python
import asyncio
import os
from livekit import rtc

async def main():
    async with rtc.Room() as room:
        @room.on("participant_connected")
        def joined(participant):
            print(participant.identity)

        @room.on("data_received")
        def received(packet):
            print(packet.participant_identity, packet.topic, packet.data)

        await room.connect(os.environ["LIVEKIT_URL"], os.environ["LIVEKIT_TOKEN"])
        await room.local_participant.publish_data(b"hello", topic="greeting")
        await asyncio.sleep(5)

asyncio.run(main())
```

`connect`, `disconnect`, `publish_data`, `publish_dtmf`, chat, one-shot text,
byte, and file streams, local name/metadata/attribute updates, and `perform_rpc` are
awaitable. The binding serializes these blocking
native calls on one room worker. Callbacks are copied and dispatched on the
Python event loop. Register event handlers before `connect`; `on` also accepts
`room.on("event", callback)`. Supported events are `connected`, `disconnected`,
`reconnecting`, `reconnected`, `connection_state_changed`, `disconnect_reason`,
`participant_connected`, `participant_disconnected`, `data_received`,
`room_metadata_changed`, `chat_message_received`, `token_refreshed`,
`recording_status_changed`, and `room_eos`.
Track subscription feedback events are `track_subscription_permission_changed`,
`track_subscription_failed`, `track_stream_state_changed`, and
`track_subscription_status_changed`. Each supplies detached track and
participant values followed by a boolean or enum status.

`room.remote_participants` gives detached participant snapshots, including
attributes and track publications, keyed by identity. Track events deliver
detached publication and participant values. `set_remote_track_subscribed`
and `update_remote_track_settings` control received tracks. Room and local
participant string properties are live getters. `room.local_participant_snapshot`
and `room.remote_participants` return detached participant values including
permissions, audio level, connection quality, speaking state, and attributes.
Room events also cover participant field changes, speaker lists, connection
quality, room snapshots and SID changes, data channel backpressure, and SIP
DTMF. Callback values are copied before dispatch on the Room event loop.
Use `rtc.list_media_devices()` and `rtc.list_screen_sources()` to enumerate
available input, output, camera, monitor, and window IDs without opening them.
`RoomOptions` covers auto subscribe, adaptive stream, dynacast, join retries,
reconnect timeout, ICE servers, ICE transport policy, a synchronous reconnect
policy, and E2EE configuration. Other C ABI capabilities remain
available through the C++ SDK and are not represented by this Python surface.

```python
options = rtc.RoomOptions(
    rtc_config=rtc.RtcConfiguration(
        ice_servers=[rtc.IceServer(["turn:relay.example:3478"], "user", "password")],
        ice_transport_type=rtc.IceTransportType.RELAY,
    ),
    reconnect_policy=lambda context: 500 if context.retry_count < 5 else None,
)
await room.connect(url, token, options)
await room.local_participant.set_track_subscription_permissions(
    allow_all_participants=False,
    participant_permissions=[rtc.ParticipantTrackPermission(
        participant_identity="viewer", allowed_track_sids=["TR_track_sid"]
    )],
)
```

The reconnect policy runs on the native recovery thread. Return a delay in
milliseconds to retry, or `None` to stop. Keep it short and synchronous; the
binding stops recovery if it raises or returns an invalid delay. Subscription
permissions can also be updated through `room.set_track_subscription_permissions`.

For dynamic credentials, pass a synchronous or asynchronous provider to
`connect_with_token_source`. The SDK calls it initially and again during token
refresh. The provider runs on the Room event loop and receives a detached
`TokenSourceFetchOptions` value, including `force_refresh`. Return
`TokenSourceResponse(server_url, participant_token)` or a `(url, token)` tuple.

```python
async def get_credentials(request: rtc.TokenSourceFetchOptions):
    return rtc.TokenSourceResponse(
        server_url=os.environ["LIVEKIT_URL"],
        participant_token=await fetch_token(request.force_refresh),
    )

await room.connect_with_token_source(
    get_credentials,
    fetch_options=rtc.TokenSourceFetchOptions(room_name="studio"),
    options=rtc.RoomOptions(auto_subscribe=True),
)
```

`room.is_recording`, `room.audio_output_device`, `room.speaker_volume`,
`room.speaker_muted`, and `room.audio_playback_stats` expose current room state.
Use `await room.set_audio_output_device(device_id)`,
`await room.set_speaker_volume(volume)`, and
`await room.set_speaker_muted(muted)` to control local playback.

Register a synchronous or asynchronous RPC handler before connecting. The
handler runs on the Room event loop and returns a string. Raise `RpcError` to
send a specific error code and optional data to the caller.

```python
async def echo(request: rtc.RpcInvocationData):
    return request.payload

room.local_participant.register_rpc_method("echo", echo)
```

Alternatively, call `room.register_rpc_method("echo", echo)` directly. Call
`unregister_rpc_method("echo")` when the handler is no longer needed.

External media sources do not open devices. Create sources, then create and
publish tracks after the Room connects:

```python
audio = rtc.AudioSource(sample_rate=48000, num_channels=1)
video = rtc.VideoSource()
try:
    await room.connect(url, token)
    audio_track = await room.local_participant.create_audio_track("tone", audio)
    video_track = await room.local_participant.create_video_track("slides", video)
    await room.local_participant.publish_track(audio_track)
    await room.local_participant.publish_track(video_track)
    await audio.capture_frame(pcm16_bytes)
    await video.capture_i420(i420_bytes, width=640, height=360)
finally:
    await room.disconnect()
    await room.aclose()  # closes Room-owned track handles
    audio.close()
    video.close()
```

Audio input is signed 16-bit interleaved PCM at the source's configured rate
and channel count. `capture_i420` accepts tightly packed I420; `capture_frame`
also accepts RGBA, RGB24, NV12, and other native video buffer formats with
optional plane layout, rotation, and frame metadata. Explicit source closure
releases native memory; a source cannot close while a track still uses it.
`TrackPublishOptions` supports primary and backup video encoding, backup
codec, frame metadata features, and degradation preference. Published video
tracks can update encoding and degradation preference. Screen share tracks
also provide `publish_screen_share_video` and `publish_screen_share_audio`.
For a subscribed remote track, use `await room.create_audio_stream(identity, sid)`
or `await room.create_video_stream(identity, sid)` to obtain a bounded reader.
`await stream.read(timeout_ms=1000)` returns a detached `AudioFrame` or
`VideoFrame`, or `None` on timeout/closure. Call `stream.close()` when done.
The `audio_frame`, `video_frame`, and `video_frame_with_metadata` room events
carry detached track, participant, and frame values. Video metadata includes
optional user timestamp, frame ID, and user bytes.
Device-backed sources use the same track API: create them with
`AudioSource.from_microphone(...)`, `AudioSource.from_system_audio(...)`,
`VideoSource.from_camera(...)`, or `VideoSource.from_screen(...)`, then call
`await source.start_capture()` and `await source.stop_capture()` as needed.
Sources expose capture status, current device or screen ID, and switching.
Microphone sources also expose mute, volume, processing options, and processing
statistics. Device constructors open the selected physical device.

To save a subscribed remote track, call
`await room.start_track_recording(identity, track_sid, output_path)`. Inspect
`recorder.stats`, then `await recorder.stop()` before reading the output file.
Call `await recorder.aclose()` to release it; Room cleanup also closes any
remaining recorders.
`LocalTrack.rtc_stats` and
`room.get_remote_track_rtc_stats(identity, track_sid)` return detached,
normalized RTC stream stats. An unavailable report yields an empty tuple.

DataTrack schemas, publishers, remote snapshots, and bounded pull readers are
available after connecting. Schema definitions and frames use `bytes`:

```python
schema_id = rtc.DataTrackSchemaId(
    "telemetry", rtc.DataTrackSchemaEncoding.JSON_SCHEMA
)
await room.store_data_track_schema(rtc.DataTrackSchema(schema_id, b'{"type":"object"}'))
publisher = await room.local_participant.publish_data_track(
    rtc.DataTrackPublishOptions(
        "telemetry", frame_encoding=rtc.DataTrackFrameEncoding.JSON,
        schema=schema_id,
    )
)
await publisher.try_push(b'{"temperature":21}', user_timestamp=123)
publication = publisher.publication

reader = await room.subscribe_data_track("publisher-identity", "DT_track_sid")
frame = await reader.read(timeout_ms=1000)
reader.close()
```

`remote_data_tracks` contains detached snapshots keyed by SID. Room events
`data_track_published`, `data_track_unpublished`,
`local_data_track_published`, `local_data_track_unpublished`, and
`data_track_frame` copy borrowed native values before dispatch.

Process-wide diagnostics are exposed through `get_log_levels()`,
`set_log_levels(...)`, `set_log_callback(...)`, `get_trace_options()`,
`set_trace_options(...)`, `set_trace_callback(...)`,
`start_trace_json_file(path)`, and `stop_trace()`. Log and trace callbacks run
on native threads; keep them short and thread safe. Pass `None` to unregister a
callback. Trace JSON output writes to the supplied local path.

For E2EE, pass `E2EEOptions` through `RoomOptions.encryption` (or its `e2ee`
alias). Peers need the same key and compatible key derivation settings. Use
`room.e2ee` or `room.e2ee_manager` to update keys and frame cryptors; both
names refer to the same manager. `encryption_state_changed` emits detached
`EncryptionState` values.

```python
key = bytes.fromhex(os.environ["LIVEKIT_E2EE_KEY_HEX"])
options = rtc.RoomOptions(encryption=rtc.E2EEOptions(shared_key=key))
await room.connect(url, token, options)
```

For incremental outgoing streams, call `await room.stream_text(...)` or
`await room.stream_bytes(...)`. The returned `StreamWriter` exposes its stream
ID and supports `await writer.write(chunk)`, `await writer.close()`, and
`await writer.cancel(reason)`. Close or cancel writers after use; Room cleanup
cancels any writers still open. `stream_progress` and `stream_complete` Room
events deliver detached `StreamProgress` and `StreamCompletion` values.
`await writer.get_info()` returns a detached metadata snapshot while the
writer is open.

Incoming completed streams emit `text_received`, `byte_received`, or
`file_received` with detached `TextMessage` or `ByteMessage` values. For
incremental streams, register a topic before connecting:

```python
room.register_text_stream_handler("transcript", lambda event: print(event.content))
room.register_byte_stream_handler("assets", lambda event: print(len(event.content)))
```

The handlers receive `TextStreamEvent` and `ByteStreamEvent` values with
`StreamEventType.OPEN`, `CHUNK`, `CLOSED`, or `FAILED`. The same values are also
available through `room.on("text_stream_event", ...)` and
`room.on("byte_stream_event", ...)`. Call `unregister_text_stream_handler` or
`unregister_byte_stream_handler` to stop a topic. The room's event queue holds
1024 events and drops the oldest when full; `room.dropped_events` reports the
count.

The native runtime is initialized once when the first `Room` is created. Do
not call `lk_shutdown()` while Python rooms may exist. Always close a room
with `async with` or `await room.aclose()`.

## Package layout

`room.py` owns room lifecycle and native operations. `_native.py` defines the
versioned C ABI structures and function signatures. `_connection.py` owns
borrowed connection and subscription option buffers. `_models.py` contains
detached public values, `_events.py` dispatches callbacks through a bounded
queue, `_token_source.py` bridges token refreshes to the event loop,
`e2ee.py` owns encryption options and key control, and
`_rpc.py` bridges RPC handlers, `media.py` owns external sources and tracks,
`data_track.py` owns DataTrack handles and detached values,
and `media_devices.py` handles read-only device enumeration. Raw callback memory
is copied before the Python event loop sees it.

## Offline checks

The event dispatcher and callback table tests run without the native library;
room tests skip until `LIVEKIT_CLIENT_CPP_LIBRARY` is set.

```powershell
$env:PYTHONPATH = 'pkg/python'
$env:LIVEKIT_CLIENT_CPP_LIBRARY = 'C:\path\to\livekitclient.dll'
python -m unittest discover -s pkg/python/tests -v
```
