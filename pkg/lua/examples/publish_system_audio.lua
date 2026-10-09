-- Mirrors examples/publish_system_audio. Opens a real output loopback device.
local livekit = require("livekit-client")
local url = arg[1] or os.getenv("LIVEKIT_URL")
local token = arg[2] or os.getenv("LIVEKIT_TOKEN")
assert(url and token, "usage: publish_system_audio.lua <url> <token> [output-device-id]")

local room = assert(livekit.new_room())
assert(room:connect(url, token))
for _ = 1, 100 do
  if room:is_connected() then break end
  assert(room:step(50))
end
assert(room:is_connected(), "room did not become connected")
local track = assert(assert(room:publish_system_audio_track_async("system-audio", {
  device_id = arg[3] or ""
})):wait(50))
print("Publishing system audio from " .. assert(room:capture_source_id(track)))
for _ = 1, 200 do assert(room:step(50)) end
assert(room:unpublish_local_track(track))
assert(room:disconnect())
assert(room:close())
