-- Opt-in dynamic token source test against a local LiveKit server.
local livekit = require("livekit-client")
local url = os.getenv("LIVEKIT_URL")
local first_token = os.getenv("LIVEKIT_TOKEN")
local second_token = os.getenv("LIVEKIT_PUBLISHER_TOKEN")
if not url or not first_token or not second_token then
  print("skipped: set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_PUBLISHER_TOKEN")
  return
end

local first = assert(livekit.new_room())
local first_calls = 0
assert(first:connect_with_token_source(function(request)
  first_calls = first_calls + 1
  assert(request.room_name == "lua-token-source" and request.participant_identity == "first")
  assert(request.participant_attributes.role == "receiver")
  return {url = url, token = first_token}
end, {
  room_name = "lua-token-source", participant_identity = "first",
  participant_attributes = {role = "receiver"}
}))
for _ = 1, 100 do
  if first:is_connected() then break end
  assert(first:step(50))
end
assert(first_calls >= 1 and first:is_connected())
assert(first:disconnect())
assert(first:close())

local second = assert(livekit.new_room())
local second_calls = 0
local future = assert(second:connect_with_token_source_async(function(request)
  second_calls = second_calls + 1
  assert(request.participant_identity == "second")
  return url, second_token
end, {participant_identity = "second"}, {join_retries = 2}))
assert(future:wait(50))
for _ = 1, 100 do
  if second:is_connected() then break end
  assert(second:step(50))
end
assert(second_calls >= 1 and second:is_connected())
assert(second:disconnect())
assert(second:close())
print("token source connected both ways")
