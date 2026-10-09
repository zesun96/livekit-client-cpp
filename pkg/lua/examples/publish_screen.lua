-- Mirrors examples/publish_screen. Selects the first monitor unless given a source ID.
local livekit = require("livekit-client")
local url = arg[1] or os.getenv("LIVEKIT_URL")
local token = arg[2] or os.getenv("LIVEKIT_TOKEN")
assert(url and token, "usage: publish_screen.lua <url> <token> [monitor-or-window-id]")
local source_id = arg[3]
if not source_id then
  local sources = assert(livekit.list_screen_sources())
  for _, source in ipairs(sources) do
    if source.kind == 0 then source_id = source.id; break end
  end
end
assert(source_id, "no screen source found")

local room = assert(livekit.new_room())
assert(room:connect(url, token))
for _ = 1, 100 do
  if room:is_connected() then break end
  assert(room:step(50))
end
assert(room:is_connected(), "room did not become connected")
local track = assert(assert(room:publish_screen_track_async("screen", {
  source_id = source_id, fps = 15, include_cursor = true
})):wait(50))
print("Publishing screen " .. assert(room:capture_source_id(track)))
for _ = 1, 200 do assert(room:step(50)) end
assert(room:unpublish_local_track(track))
assert(room:disconnect())
assert(room:close())
