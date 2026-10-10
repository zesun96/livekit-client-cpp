-- Receives decoded media through callbacks, matching the C++ room event example.
local livekit = require("livekit-client")
local url = arg[1] or os.getenv("LIVEKIT_URL")
local token = arg[2] or os.getenv("LIVEKIT_TOKEN")
assert(url and token, "usage: receive_media_events.lua <url> <token> [seconds]")
local seconds = tonumber(arg[3]) or 15
assert(seconds >= 0, "seconds must be nonnegative")
local room = assert(livekit.new_room())
local counts = {}
assert(room:set_media_frame_events({audio = true, video = true, capacity = 32,
  max_bytes = 16 * 1024 * 1024}))
assert(room:on(function(event)
  if event.type == "audio_frame" or event.type == "video_frame" then
    counts[event.sid] = (counts[event.sid] or 0) + 1
    if counts[event.sid] == 1 then
      print(string.format("%s from %s: %s (%d bytes)", event.type, event.identity,
        event.sid, #event.frame.data))
    end
    -- Consume PCM/I420 here; callbacks run on the Lua event loop thread.
  end
end))
assert(room:connect(url, token))
local deadline = os.time() + seconds
while os.time() < deadline do
  assert(room:step(20))
end
for sid, count in pairs(counts) do print(string.format("%s: %d frames", sid, count)) end
print(string.format("Dropped frame events: %d", room:media_frame_event_stats().dropped))
assert(room:set_media_frame_events({}))
assert(room:close())
