-- Replace the environment lookup with an authenticated token service in production.
local livekit = require("livekit-client")
local url = arg[1] or os.getenv("LIVEKIT_URL")
local token = arg[2] or os.getenv("LIVEKIT_TOKEN")
assert(url and token, "usage: token_source.lua <url> <token>")

local room = assert(livekit.new_room())
assert(room:connect_with_token_source(function(request)
  print("fetch token, force refresh:", request.force_refresh)
  return {url = url, token = token}
end))
for _ = 1, 100 do
  if room:is_connected() then break end
  assert(room:step(50))
end
assert(room:is_connected())
for _ = 1, 100 do assert(room:step(50)) end
assert(room:disconnect())
assert(room:close())
