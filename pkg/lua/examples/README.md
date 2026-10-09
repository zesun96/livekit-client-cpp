# Lua examples

These examples follow the corresponding [C++ examples](../../../examples/README.md) using APIs
available in the Lua binding. Run them with a Lua interpreter linked to the same Lua runtime DLL
as `livekit_client_native.dll`. On Windows, the optional `livekit_lua_test_runner.exe` built with
`-DLKC_LUA_BUILD_TEST_RUNNER=ON` can also run examples and forward their arguments.

Set `LUA_PATH` to include `pkg/lua/lua/?.lua`, `LUA_CPATH` to include the directory containing
`livekit_client_native.dll`, and `PATH` to include the Lua and LiveKit runtime DLL directories.
Use the RTC endpoint including `/rtc` as the URL. Supply a short-lived participant token through
the command line or `LIVEKIT_URL` and `LIVEKIT_TOKEN` environment variables.

```powershell
$runner = "out/build/lua-release/Release/livekit_lua_test_runner.exe"
$url = "http://localhost:7880/rtc"
& $runner pkg/lua/examples/media_devices.lua
& $runner pkg/lua/examples/cpp_sample.lua $url $token
& $runner pkg/lua/examples/receive_media.lua $url $receiverToken 15
& $runner pkg/lua/examples/publish_audio.lua $url $senderToken
& $runner pkg/lua/examples/publish_video.lua $url $senderToken
& $runner pkg/lua/examples/publish_microphone.lua $url $senderToken
& $runner pkg/lua/examples/publish_system_audio.lua $url $senderToken
& $runner pkg/lua/examples/publish_camera.lua $url $senderToken
& $runner pkg/lua/examples/publish_screen.lua $url $senderToken
& $runner pkg/lua/examples/room_event.lua $url $receiverToken 30
& $runner pkg/lua/examples/data_transfer.lua $url $senderToken C:/path/to/file.bin
& $runner pkg/lua/examples/stream_writer.lua $url $senderToken
& $runner pkg/lua/examples/data_track_schema.lua $url $senderToken
& $runner pkg/lua/examples/token_source.lua $url $senderToken
& $runner pkg/lua/examples/rpc_receiver.lua $url $receiverToken 30
& $runner pkg/lua/examples/rpc_caller.lua $url $senderToken receiver-identity "hello"
```

`media_devices.lua` only enumerates devices; it does not open a microphone, camera, or speaker.
`room_event.lua` prints room, participant, track, data, chat, and stream events, including
subscription feedback. `receive_media.lua` explicitly subscribes to published audio/video tracks
and reads decoded PCM/I420 frames. `publish_audio.lua` sends a 440 Hz synthetic tone and
`publish_video.lua` sends synthetic RGBA frames, matching the C++ examples without opening
physical devices. Start `receive_media.lua` first in another terminal.

`publish_microphone.lua`, `publish_system_audio.lua`, `publish_camera.lua`, and
`publish_screen.lua` open real capture devices for 10 seconds. Pass an optional device ID to the
first three or a monitor/window source ID to the last; the screen example selects the first
monitor by default. Use `media_devices.lua` or `livekit.list_screen_sources()` to inspect IDs.

`token_source.lua` shows the dynamic-credential callback shape. Its environment lookup is an
example only; replace it with your own short-lived token service.

`data_transfer.lua` sends one-shot text, bytes, an optional file, and a data message using
coroutine futures. `stream_writer.lua` sends text in three chunks. `data_track_schema.lua`
publishes a JSON DataTrack frame and requires `enable_participant_data_blob: true` on the server.
Start `rpc_receiver.lua` in one
terminal and `rpc_caller.lua` in another. Use different participant identities in the same room,
and pass the receiver's printed identity to the caller. Registered Lua RPC handlers run when the
Lua thread calls `room:poll()` or `room:step()`; they return a response string or a table with
`payload`, `error_code`, `error_message`, and `error_data`.
