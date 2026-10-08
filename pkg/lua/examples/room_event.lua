-- Mirrors the room event portion of examples/room_event. Media frames need a separate binding.
local livekit = require("livekit-client")
local url = arg[1] or os.getenv("LIVEKIT_URL")
local token = arg[2] or os.getenv("LIVEKIT_TOKEN")
assert(url and token, "usage: room_event.lua <url> <token> [seconds]")
local seconds = tonumber(arg[3]) or 30
assert(seconds >= 0, "seconds must be nonnegative")

local room = assert(livekit.new_room())
assert(room:on(function(event)
  local from = event.participant_identity or event.identity or ""
  if event.type == "data_received" or event.type == "byte_received" or
      event.type == "file_received" then
    print(string.format("%s from=%s topic=%s bytes=%d", event.type, from,
      event.topic or "", #(event.data or "")))
  elseif event.type == "text_received" then
    print(string.format("text from=%s topic=%s: %s", from, event.topic, event.text))
  elseif event.type == "chat_message_received" then
    print(string.format("chat from=%s id=%s: %s", from, event.id, event.message))
  elseif event.type == "track_subscription_failed" then
    print(string.format("track %s subscription failed: %s", event.sid, event.error))
  elseif event.type == "track_subscription_status_changed" or
      event.type == "track_stream_state_changed" then
    print(string.format("%s track=%s value=%s", event.type, event.sid,
      event.status or event.state))
  elseif event.type == "disconnected" then
    print(string.format("disconnected reason=%s", event.reason))
  else
    print(string.format("%s identity=%s sid=%s", event.type, from, event.sid or ""))
  end
end))

local ok, err = room:connect(url, token)
assert(ok, err)
print("Listening as " .. room:local_identity())
local deadline = os.time() + seconds
while room:is_connected() and os.time() < deadline do
  local count, callback_error = room:step(100)
  assert(count, callback_error)
end
if room:is_connected() then
  ok, err = room:disconnect()
  assert(ok, err)
end
assert(room:close())
