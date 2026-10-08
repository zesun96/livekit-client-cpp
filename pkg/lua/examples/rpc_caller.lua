-- Mirrors the caller half of examples/rpc. Start the C++ rpc receiver first.
local livekit = require("livekit-client")
local url = arg[1] or os.getenv("LIVEKIT_URL")
local token = arg[2] or os.getenv("LIVEKIT_TOKEN")
local destination = arg[3]
assert(url and token and destination,
  "usage: rpc_caller.lua <url> <token> <destination-identity> [payload]")
local payload = arg[4] or "hello"

local room = assert(livekit.new_room())
local ok, err = livekit.run(room, function()
  local connected, connect_error = assert(room:connect_async(url, token)):await()
  assert(connected, connect_error)
  local result, rpc_error = assert(room:perform_rpc_async(destination,
    "example.echo", payload)):await()
  assert(result, rpc_error)
  assert(result.ok, string.format("RPC error %d: %s", result.error_code,
    result.error_message))
  print("RPC response: " .. result.payload)
  return assert(room:disconnect_async()):await()
end)
assert(ok, err)
assert(room:close())
