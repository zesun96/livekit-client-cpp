-- Mirrors examples/cpp_sample: connect, report identity, disconnect.
local livekit = require("livekit-client")
local url = arg[1] or os.getenv("LIVEKIT_URL")
local token = arg[2] or os.getenv("LIVEKIT_TOKEN")
assert(url and token, "usage: cpp_sample.lua <url> <token> (or set LIVEKIT_URL and LIVEKIT_TOKEN)")

local room = assert(livekit.new_room())
local ok, err = room:connect(url, token)
assert(ok, err)
print(string.format("Connected as %s (%s)", room:local_identity(), room:local_sid()))
ok, err = room:disconnect()
assert(ok, err)
assert(room:close())
