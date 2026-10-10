-- Mirrors the media subscription and pull-reader portion of examples/room_event.
local livekit = require("livekit-client")
local url = arg[1] or os.getenv("LIVEKIT_URL")
local token = arg[2] or os.getenv("LIVEKIT_TOKEN")
assert(url and token, "usage: receive_media.lua <url> <token> [seconds]")
local seconds = tonumber(arg[3]) or 15
assert(seconds >= 0, "seconds must be nonnegative")

local room = assert(livekit.new_room())
local streams = {}
assert(room:on(function(event)
  if event.type == "track_published" then
    assert(room:set_remote_track_subscribed(event.participant_sid, event.sid, true))
  elseif event.type == "track_subscribed" then
    local stream
    if event.kind == 1 then
      stream = assert(room:open_audio_stream(event.participant_identity, event.sid))
    elseif event.kind == 2 then
      stream = assert(room:open_video_stream(event.participant_identity, event.sid))
    end
    if stream then
      streams[#streams + 1] = {id = stream, kind = event.kind, sid = event.sid,
        identity = event.participant_identity, frames = 0}
      print("Subscribed " .. event.sid)
    end
  end
end))
assert(room:connect(url, token, {auto_subscribe = false}))
for _ = 1, 100 do
  if room:is_connected() then break end
  assert(room:step(50))
end
assert(room:is_connected(), "room did not become connected")
local deadline = os.time() + seconds
while room:is_connected() and os.time() < deadline do
  assert(room:step(20))
  for _, stream in ipairs(streams) do
    while true do
      local frame, err
      if stream.kind == 1 then
        frame, err = room:read_audio_frame(stream.id)
      else
        frame, err = room:read_video_frame(stream.id)
      end
      if not frame then
        assert(err == "empty" or err == "closed", err)
        break
      end
      stream.frames = stream.frames + 1
      if stream.frames == 1 then
        print(string.format("First %s frame from %s (%d bytes)",
          stream.kind == 1 and "audio" or "video", stream.sid, #frame.data))
      end
    end
  end
end
for _, stream in ipairs(streams) do
  print(string.format("%s: %d frames", stream.sid, stream.frames))
  local stats = assert(room:remote_track_rtc_stats(stream.identity, stream.sid))
  print(string.format("%s: %d RTC statistic streams", stream.sid, #stats))
  assert(room:close_remote_stream(stream.id))
end
if room:is_connected() then assert(room:disconnect()) end
assert(room:close())
