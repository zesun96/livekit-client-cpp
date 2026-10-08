-- Mirrors examples/publish_audio with synthetic 48 kHz mono PCM.
local livekit = require("livekit-client")
local url = arg[1] or os.getenv("LIVEKIT_URL")
local token = arg[2] or os.getenv("LIVEKIT_TOKEN")
assert(url and token, "usage: publish_audio.lua <url> <token> [allowed-subscriber]")

local room = assert(livekit.new_room())
if arg[3] then
  assert(room:set_track_subscription_permissions(false, {
    {participant_identity = arg[3], allow_all = true}
  }))
end
assert(room:connect(url, token))
for _ = 1, 100 do
  if room:is_connected() then break end
  assert(room:step(50))
end
assert(room:is_connected(), "room did not become connected")

local track = assert(room:publish_audio_track("lua-tone", 48000, 1))
local function tone_frame(frame_number)
  local parts = {}
  for i = 0, 479 do
    local sample = math.floor(math.sin((frame_number * 480 + i) * math.pi * 2 * 440 / 48000) * 6000)
    if sample < 0 then sample = sample + 65536 end
    parts[#parts + 1] = string.char(sample % 256, math.floor(sample / 256))
  end
  return table.concat(parts)
end
for frame_number = 0, 499 do
  local pcm = tone_frame(frame_number)
  local sent, err = room:push_audio_frame(track, pcm)
  if not sent then
    assert(room:step(20))
    sent, err = room:push_audio_frame(track, pcm)
    assert(sent, err)
  end
  assert(room:step(10))
end
print("Published a 440 Hz audio tone for 5 seconds")
assert(room:unpublish_local_track(track))
assert(room:disconnect())
assert(room:close())
