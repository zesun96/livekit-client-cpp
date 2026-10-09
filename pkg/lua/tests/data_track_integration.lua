-- Opt-in DataTrack test; the server must enable participant data blobs.
local livekit = require("livekit-client")
local url = os.getenv("LIVEKIT_URL")
local receiver_token = os.getenv("LIVEKIT_TOKEN")
local publisher_token = os.getenv("LIVEKIT_PUBLISHER_TOKEN")
if not url or not receiver_token or not publisher_token then
  print("skipped: set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_PUBLISHER_TOKEN")
  return
end

local receiver = assert(livekit.new_room())
local publisher = assert(livekit.new_room())
assert(receiver:connect(url, receiver_token))
assert(publisher:connect(url, publisher_token))
for _ = 1, 100 do
  if receiver:is_connected() and publisher:is_connected() then break end
  assert(receiver:step(50))
  assert(publisher:step(50))
end
assert(receiver:is_connected() and publisher:is_connected())
local schema_id = {name = "lua.telemetry.v1", encoding = livekit.DATA_TRACK_SCHEMA_ENCODING.JSON_SCHEMA}
local definition = '{"type":"object","properties":{"value":{"type":"number"}}}'
assert(assert(publisher:store_data_track_schema_async(schema_id, definition)):wait(50))
local track = assert(assert(publisher:publish_data_track_async("lua-telemetry", {
  frame_encoding = livekit.DATA_TRACK_FRAME_ENCODING.JSON, schema = schema_id
})):wait(50))
local local_info = assert(publisher:data_track_info(track))
assert(local_info.is_published and local_info.name == "lua-telemetry")

local remote
for _ = 1, 100 do
  assert(receiver:step(50))
  assert(publisher:poll())
  for _, item in ipairs(assert(receiver:remote_data_tracks())) do
    if item.name == "lua-telemetry" then remote = item; break end
  end
  if remote then break end
end
assert(remote and remote.sid ~= "", "remote DataTrack was not announced")
local schema = assert(assert(receiver:get_data_track_schema_async("publisher", schema_id)):wait(50))
assert(schema.name == schema_id.name and schema.definition == definition)
local reader = assert(assert(receiver:subscribe_data_track_async("publisher", remote.sid, {
  buffer_capacity = 8, max_partial_frames = 8
})):wait(50))
local frame
for _ = 1, 100 do
  assert(publisher:data_track_push(track, '{"value":42}', 1234))
  frame = receiver:read_data_track_frame(reader, 50)
  if frame then break end
  assert(publisher:poll())
end
assert(frame and frame.data == '{"value":42}' and frame.user_timestamp == 1234)
assert(receiver:data_track_reader_stats(reader).dropped_frames == 0)
assert(receiver:close_data_track_reader(reader))
assert(publisher:unpublish_data_track(track))
assert(publisher:disconnect())
assert(receiver:disconnect())
assert(publisher:close())
assert(receiver:close())
print("DataTrack frame delivered")
