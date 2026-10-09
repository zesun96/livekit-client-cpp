-- Opt-in desktop capture test against a local LiveKit server.
local livekit = require("livekit-client")
io.stdout:setvbuf("no")
local url = os.getenv("LIVEKIT_URL")
local receiver_token = os.getenv("LIVEKIT_TOKEN")
local publisher_token = os.getenv("LIVEKIT_PUBLISHER_TOKEN")
if not url or not receiver_token or not publisher_token then
  print("skipped: set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_PUBLISHER_TOKEN")
  return
end

local source_id
for _, source in ipairs(assert(livekit.list_screen_sources())) do
  if source.kind == 0 then source_id = source.id; break end
end
if not source_id then
  print("skipped: no monitor capture source is available")
  return
end

local receiver = assert(livekit.new_room())
local publisher = assert(livekit.new_room())
local stream
assert(receiver:on(function(event)
  if event.type == "track_subscribed" and event.kind == 2 then
    stream = assert(receiver:open_video_stream(event.participant_identity, event.sid))
  end
end))
assert(receiver:connect(url, receiver_token))
assert(publisher:connect(url, publisher_token))
for _ = 1, 100 do
  if receiver:is_connected() and publisher:is_connected() then break end
  assert(receiver:step(50))
  assert(publisher:step(50))
end
assert(receiver:is_connected() and publisher:is_connected())

local track = assert(assert(publisher:publish_screen_track_async("lua-screen", {
  source_id = source_id, fps = 10, include_cursor = false
})):wait(50))
assert(publisher:capture_is_running(track))
assert(publisher:capture_source_id(track) == source_id)
local frames = 0
for _ = 1, 200 do
  assert(receiver:step(25))
  assert(publisher:poll())
  if stream then
    while true do
      local frame, err = receiver:read_video_frame(stream)
      if not frame then assert(err == "empty" or err == "closed", err); break end
      assert(frame.width > 0 and frame.height > 0 and #frame.data > 0)
      frames = frames + 1
    end
  end
  if frames >= 3 then break end
end
assert(frames >= 3, "screen capture did not deliver video frames")
assert(assert(publisher:stop_capture_async(track)):wait(50))
assert(not publisher:capture_is_running(track))
assert(assert(publisher:start_capture_async(track)):wait(50))
assert(publisher:capture_is_running(track))
assert(assert(publisher:switch_capture_source_async(track, source_id)):wait(50))
assert(publisher:unpublish_local_track(track))
if stream then assert(receiver:close_remote_stream(stream)) end
assert(publisher:disconnect())
assert(receiver:disconnect())
assert(publisher:close())
assert(receiver:close())
print("received captured screen frames=" .. frames)
