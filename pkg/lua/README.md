# livekit-client Lua binding

This is a first Lua binding for the stable C ABI of `livekit-client-cpp`. It supports Lua 5.1+
and currently exposes room connection, disconnect, connection state, participant arrival/departure,
data messages, and cleanup. The Lua interpreter, Lua import library, and LiveKit DLL must all have
the same architecture. On Windows use an x64 Lua build with the x64 LiveKit SDK.

## Build on Windows

Install or build the shared LiveKitClient package, then configure this directory separately:

```powershell
cmake -S pkg/lua -B out/build/lua -G "Visual Studio 17 2022" -A x64 `
  -DLUA_INCLUDE_DIR=C:/path/to/lua/include `
  -DLUA_LIBRARY=C:/path/to/lua/lib/lua.lib `
  -DLiveKitClient_DIR=C:/path/to/livekit/lib/cmake/LiveKitClient
cmake --build out/build/lua --config Release
```

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
Connection options currently support `auto_subscribe`, `adaptive_stream`, and `dynacast`.
Fallible methods return `true` or `nil, message`. Data payloads are binary-safe Lua strings.

## Coroutine asynchronous calls

The same three operations have `connect_async`, `disconnect_async`, and `publish_data_async`
variants. Each returns a future or `nil, message`. Futures support `:result()` (nonblocking),
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

The binding initializes the LiveKit runtime when loaded. The runtime remains active until process
exit; do not call `lk_shutdown()` externally while Lua rooms may still exist. Media tracks, streams,
RPC, E2EE, and the remaining C API surface are not wrapped yet.
