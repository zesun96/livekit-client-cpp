-- Mirrors examples/data_track_schema. The server must enable participant data blobs.
local livekit = require("livekit-client")
local url = arg[1] or os.getenv("LIVEKIT_URL")
local token = arg[2] or os.getenv("LIVEKIT_TOKEN")
assert(url and token, "usage: data_track_schema.lua <url> <token>")

local room = assert(livekit.new_room())
assert(room:connect(url, token))
local schema = {name = "example.telemetry.v1", encoding = livekit.DATA_TRACK_SCHEMA_ENCODING.JSON_SCHEMA}
local definition = '{"type":"object","properties":{"temperature":{"type":"number"}}}'
assert(room:store_data_track_schema(schema, definition))
local track = assert(room:publish_data_track("example-telemetry", {
  frame_encoding = livekit.DATA_TRACK_FRAME_ENCODING.JSON, schema = schema
}))
assert(room:data_track_push(track, '{"temperature":21.5}', 1))
for _ = 1, 20 do assert(room:step(50)) end
assert(room:unpublish_data_track(track))
assert(room:disconnect())
assert(room:close())
