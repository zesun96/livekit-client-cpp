-- Mirrors examples/publish_microphone. Opens a real microphone.
local livekit = require("livekit-client")
local url = arg[1] or os.getenv("LIVEKIT_URL")
local token = arg[2] or os.getenv("LIVEKIT_TOKEN")
assert(url and token, "usage: publish_microphone.lua <url> <token> [device-id]")

local room = assert(livekit.new_room())
assert(room:connect(url, token))
for _ = 1, 100 do
  if room:is_connected() then break end
  assert(room:step(50))
end
assert(room:is_connected(), "room did not become connected")
local track = assert(assert(room:publish_microphone_track_async("microphone", {
  device_id = arg[3] or "", echo_cancellation = true,
  auto_gain_control = true, noise_suppression = true
})):wait(50))
print("Publishing microphone " .. assert(room:capture_source_id(track)))
for _ = 1, 200 do assert(room:step(50)) end
print("Microphone processed frames: " .. assert(room:microphone_processing_stats(track)).capture_frames_processed)
assert(room:unpublish_local_track(track))
assert(room:disconnect())
assert(room:close())
