-- Mirrors examples/publish_video with synthetic 160x90 RGBA frames.
local livekit = require("livekit-client")
local url = arg[1] or os.getenv("LIVEKIT_URL")
local token = arg[2] or os.getenv("LIVEKIT_TOKEN")
assert(url and token, "usage: publish_video.lua <url> <token>")

local room = assert(livekit.new_room())
assert(room:connect(url, token))
for _ = 1, 100 do
  if room:is_connected() then break end
  assert(room:step(50))
end
assert(room:is_connected(), "room did not become connected")

local width, height = 160, 90
local function rgba_frame(phase)
  return string.rep(string.char(phase % 256, (phase * 2) % 256, 180, 255), width * height)
end
local track = assert(room:publish_video_track("lua-video", rgba_frame(0), width, height))
for i = 1, 150 do
  assert(room:push_video_frame(track, rgba_frame(i), width, height))
  assert(room:step(33))
end
print("Published 160x90 RGBA video for 5 seconds")
assert(room:unpublish_local_track(track))
assert(room:disconnect())
assert(room:close())
