-- Mirrors the receiver half of examples/rpc.
local livekit = require("livekit-client")
local url = arg[1] or os.getenv("LIVEKIT_URL")
local token = arg[2] or os.getenv("LIVEKIT_TOKEN")
assert(url and token, "usage: rpc_receiver.lua <url> <token> [seconds]")
local seconds = tonumber(arg[3]) or 30
assert(seconds >= 0, "seconds must be nonnegative")

local room = assert(livekit.new_room())
assert(room:register_rpc_method("example.echo", function(request)
  print("RPC from " .. request.caller_identity .. ": " .. request.payload)
  return "echo:" .. request.payload
end))
local ok, err = room:connect(url, token)
assert(ok, err)
print("RPC receiver ready as " .. room:local_identity())
local deadline = os.time() + seconds
while room:is_connected() and os.time() < deadline do
  local count, callback_error = room:step(100)
  assert(count, callback_error)
end
if room:is_connected() then
  ok, err = room:disconnect()
  assert(ok, err)
end
assert(room:close())
