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
& $runner pkg/lua/examples/room_event.lua $url $receiverToken 30
& $runner pkg/lua/examples/data_transfer.lua $url $senderToken C:/path/to/file.bin
& $runner pkg/lua/examples/rpc_caller.lua $url $senderToken receiver-identity "hello"
```

`media_devices.lua` only enumerates devices; it does not open a microphone, camera, or speaker.
`room_event.lua` prints room, participant, track, data, chat, and stream events, including
subscription feedback. It does not consume audio or video frames because local media and frame
readers are not yet exposed by the Lua binding. `data_transfer.lua` sends one-shot text, bytes,
an optional file, and a data message using coroutine futures; incremental stream writers remain
unavailable. `rpc_caller.lua` calls `example.echo` on the C++ `rpc` receiver because inbound Lua
RPC handler registration is not yet exposed. Run the C++ receiver in another terminal with a
different participant identity in the same room.
