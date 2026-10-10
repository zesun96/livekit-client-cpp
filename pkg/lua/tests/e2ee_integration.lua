-- Opt-in E2EE toggle test against a local LiveKit server.
local livekit = require("livekit-client")
local url, token = os.getenv("LIVEKIT_URL"), os.getenv("LIVEKIT_TOKEN")
if not url or not token then
  print("skipped: set LIVEKIT_URL and LIVEKIT_TOKEN")
  return
end

local room = assert(livekit.new_room())
local key = string.rep("k", 32)
assert(room:connect(url, token, {e2ee = {enabled = true, shared_key = key}}))
for _ = 1, 100 do
  if room:is_connected() then break end
  assert(room:step(50))
end
assert(room:is_connected(), "E2EE room did not become connected")
assert(room:e2ee_is_configured() and room:e2ee_is_enabled())
local track = assert(room:publish_audio_track("e2ee-toggle", 48000, 1, 200,
  {dtx = false}))
assert(assert(room:e2ee_set_enabled_async(false)):wait(50))
assert(not room:e2ee_is_enabled())
assert(assert(room:e2ee_set_enabled_async(true)):wait(50))
assert(room:e2ee_is_enabled())
assert(room:unpublish_local_track(track))
assert(room:close())
print("E2EE asynchronous toggle passed")
