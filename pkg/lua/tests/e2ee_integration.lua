-- Opt-in E2EE toggle test against a local LiveKit server.
local livekit = require("livekit-client")
local url, token = os.getenv("LIVEKIT_URL"), os.getenv("LIVEKIT_PUBLISHER_TOKEN")
local receiver_token = os.getenv("LIVEKIT_TOKEN")
if not url or not token or not receiver_token then
  print("skipped: set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_PUBLISHER_TOKEN")
  return
end

local room = assert(livekit.new_room())
local key = string.rep("k", 32)
assert(room:connect(url, token, {e2ee = {enabled = true, shared_key = key}}))
for _ = 1, 100 do
  if room:is_connected() then break end
  assert(room:step(50))
end
assert(room:is_connected(), "E2EE room did not become connected")
assert(room:e2ee_is_configured() and room:e2ee_is_enabled())
assert(room:e2ee_export_shared_key(0) == key)
local alternate_key = string.rep("a", 32)
assert(room:e2ee_set_shared_key(alternate_key, 1))
assert(room:e2ee_export_shared_key(1) == alternate_key)
assert(room:e2ee_set_data_key_index(1))
assert(room:e2ee_data_key_index() == 1)
assert(room:e2ee_set_data_key_index(0))
assert(room:e2ee_ratchet_shared_key(1))
assert(room:e2ee_export_shared_key(1) ~= alternate_key)
assert(room:e2ee_remove_shared_key(1))
local removed_key, removed_error = room:e2ee_export_shared_key(1)
assert(removed_key == nil and type(removed_error) == "string")
local identity, participant_key = "lua-key-test", string.rep("p", 32)
assert(room:e2ee_set_participant_key(identity, participant_key, 2))
assert(room:e2ee_export_participant_key(identity, 2) == participant_key)
assert(room:e2ee_ratchet_participant_key(identity, 2))
assert(room:e2ee_export_participant_key(identity, 2) ~= participant_key)
assert(room:e2ee_remove_participant_key(identity, 2))
assert(room:e2ee_set_participant_key(identity, participant_key, 2))
assert(room:e2ee_remove_participant_keys(identity))
local missing_participant_key = room:e2ee_export_participant_key(identity, 2)
assert(missing_participant_key == nil)
local track = assert(room:publish_audio_track("e2ee-toggle", 48000, 1, 200,
  {dtx = false}))
local cryptors = assert(room:e2ee_frame_cryptors())
assert(#cryptors == 1)
local sender = cryptors[1]
assert(sender.direction == livekit.FRAME_CRYPTOR_DIRECTION.SENDER)
assert(sender.track_sid ~= "" and sender.enabled)
assert(room:e2ee_set_frame_cryptor_enabled(sender.track_sid, sender.direction, false))
assert(not assert(room:e2ee_frame_cryptors())[1].enabled)
assert(room:e2ee_set_frame_cryptor_enabled(sender.track_sid, sender.direction, true))
assert(room:e2ee_set_shared_key(alternate_key, 1))
assert(room:e2ee_set_frame_cryptor_key_index(sender.track_sid, sender.direction, 1))
assert(assert(room:e2ee_frame_cryptors())[1].key_index == 1)
assert(room:e2ee_set_frame_cryptor_key_index(sender.track_sid, sender.direction, 0))
assert(room:e2ee_remove_shared_key(1))
local receiver = assert(livekit.new_room())
local audio_stream, audio_sid, audio_identity
assert(receiver:on(function(event)
  if event.type == "track_subscribed" and event.kind == 1 then
    if audio_stream then assert(receiver:close_remote_stream(audio_stream)) end
    audio_stream = assert(receiver:open_audio_stream(event.participant_identity, event.sid))
    audio_sid = event.sid
    audio_identity = event.participant_identity
  end
end))
assert(receiver:connect(url, receiver_token, {e2ee = {enabled = true, shared_key = key}}))
local pcm = string.rep(string.char(1, 2), 480)
local function receive_audio(previous_sid)
  local frames = 0
  for _ = 1, 200 do
    assert(room:push_audio_frame(track, pcm))
    assert(receiver:step(10))
    assert(room:poll())
    room:wait(10)
    if audio_stream and audio_sid ~= previous_sid then
      while receiver:read_audio_frame(audio_stream) do frames = frames + 1 end
    end
    if frames >= 3 then break end
  end
  assert(audio_sid and audio_sid ~= previous_sid and frames >= 3,
    "encrypted audio was not received")
end
receive_audio()
assert(receiver:e2ee_set_participant_enabled(audio_identity, false) == 1)
assert(receiver:e2ee_set_participant_enabled(audio_identity, true) == 1)
local original_sid = audio_sid
assert(assert(room:e2ee_set_enabled_async(false)):wait(50))
assert(not room:e2ee_is_enabled())
assert(assert(room:e2ee_set_enabled_async(true)):wait(50))
assert(room:e2ee_is_enabled())
receive_audio(original_sid)
assert(room:unpublish_local_track(track))
assert(room:e2ee_clear_keys())
if audio_stream then assert(receiver:close_remote_stream(audio_stream)) end
assert(receiver:close())
assert(room:close())
print("E2EE media and asynchronous toggle passed")
