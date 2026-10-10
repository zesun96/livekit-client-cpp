-- Records each subscribed remote track to a separate WAV or encoded video file.
local livekit = require("livekit-client")
local url = arg[1] or os.getenv("LIVEKIT_URL")
local token = arg[2] or os.getenv("LIVEKIT_TOKEN")
local output_dir = arg[3] or "."
local seconds = tonumber(arg[4]) or 15
assert(url and token, "usage: record_remote.lua <url> <token> [output_dir] [seconds]")
assert(seconds >= 0, "seconds must be nonnegative")

local room = assert(livekit.new_room())
local recorders = {}
assert(room:on(function(event)
  if event.type == "track_published" then
    assert(room:set_remote_track_subscribed(event.participant_sid, event.sid, true))
  elseif event.type == "track_subscribed" then
    local path = output_dir .. "/" .. event.sid
    local recorder = assert(room:start_track_recording(
      event.participant_identity, event.sid, path))
    recorders[#recorders + 1] = recorder
    print("Recording " .. event.sid .. " to " .. path)
  end
end))
assert(room:connect(url, token, {auto_subscribe = false}))
for _ = 1, 100 do
  if room:is_connected() then break end
  assert(room:step(50))
end
assert(room:is_connected(), "room did not become connected")
local deadline = os.time() + seconds
while os.time() < deadline and room:is_connected() do
  assert(room:step(50))
end
for _, recorder in ipairs(recorders) do
  assert(room:stop_track_recording(recorder))
  local stats = assert(room:track_recording_stats(recorder))
  print(string.format("%s: %d frames, %d bytes, %d dropped", stats.output_path,
    stats.frames_written, stats.bytes_written, stats.frames_dropped))
  if stats.error ~= "" then print("Recorder error: " .. stats.error) end
  assert(room:close_track_recording(recorder))
end
if room:is_connected() then assert(room:disconnect()) end
assert(room:close())
