local livekit = require("livekit-client")
io.stdout:setvbuf("no")
local url = os.getenv("LIVEKIT_URL")
local receiver_token = os.getenv("LIVEKIT_TOKEN")
local publisher_token = os.getenv("LIVEKIT_PUBLISHER_TOKEN")
if not url or not receiver_token or not publisher_token then
  print("skipped: set LIVEKIT_URL, LIVEKIT_TOKEN, and LIVEKIT_PUBLISHER_TOKEN")
  return
end

local receiver = assert(livekit.new_room())
local publisher = assert(livekit.new_room())
local audio_stream, video_stream
local audio_sid, video_sid
local subscription_futures = {}
local local_subscribed = {}
assert(publisher:on(function(event)
  if event.type == "local_track_subscribed" then local_subscribed[event.sid] = true end
end))
assert(receiver:on(function(event)
  if event.type == "track_published" then
    subscription_futures[#subscription_futures + 1] = assert(
      receiver:set_remote_track_subscribed_async(event.participant_sid, event.sid, true))
  elseif event.type == "track_subscribed" then
    if event.kind == 1 then
      audio_stream = assert(receiver:open_audio_stream(event.participant_identity, event.sid))
      audio_sid = event.sid
    elseif event.kind == 2 then
      video_stream = assert(receiver:open_video_stream(event.participant_identity, event.sid))
      video_sid = event.sid
    end
  end
end))
assert(receiver:connect(url, receiver_token, {auto_subscribe = false}))
assert(publisher:connect(url, publisher_token))
for _ = 1, 100 do
  if receiver:is_connected() and publisher:is_connected() then break end
  assert(receiver:step(50))
  assert(publisher:step(50))
end
assert(receiver:is_connected() and publisher:is_connected(), "rooms did not become connected")

local audio = assert(assert(publisher:publish_audio_track_async("lua-tone", 48000, 1, 200,
  {dtx = false, red = false, stream = "lua-media"})):wait(50))
local width, height = 160, 90
local rgba = string.rep(string.char(40, 100, 180, 255), width * height)
local video = assert(assert(publisher:publish_video_track_async("lua-video", rgba, width, height,
  "RGBA", false, {simulcast = false, stream = "lua-media", video_codec = livekit.VIDEO_CODEC.VP8,
    video_encoding = {max_bitrate = 500000, max_framerate = 15},
    frame_metadata_features = {user_timestamp = true, frame_id = true, user_data = true}})):wait(50))
local invalid_metadata, invalid_metadata_error = publisher:push_video_frame(
  video, rgba, width, height, "RGBA", nil, {frame_id = -1})
assert(invalid_metadata == nil and invalid_metadata_error == "frame_id is out of range")
assert(publisher:audio_source_queued_duration_ms(audio) >= 0)
assert(publisher:update_video_encoding(video, {max_bitrate = 500000, max_framerate = 15}))
assert(publisher:update_video_degradation_preference(
  video, livekit.VIDEO_DEGRADATION_PREFERENCE.BALANCED))

local function tone_frame()
  local parts = {}
  for i = 0, 479 do
    local sample = math.floor(math.sin(i * math.pi * 2 * 440 / 48000) * 6000)
    if sample < 0 then sample = sample + 65536 end
    parts[#parts + 1] = string.char(sample % 256, math.floor(sample / 256))
  end
  return table.concat(parts)
end
local pcm = tone_frame()
local audio_frames, video_frames = 0, 0
for i = 1, 300 do
  assert(publisher:push_audio_frame(audio, pcm))
  if i % 3 == 1 then
    assert(publisher:push_video_frame(video, rgba, width, height, "RGBA", nil,
      {user_timestamp_us = i * 1000, frame_id = i, user_data = "lua"}))
  end
  assert(receiver:step(10))
  assert(publisher:poll())
  if audio_stream then
    while true do
      local frame, err = receiver:read_audio_frame(audio_stream)
      if not frame then assert(err == "empty" or err == "closed", err); break end
      assert(#frame.data == frame.samples_per_channel * frame.channels * 2)
      audio_frames = audio_frames + 1
    end
  end
  if video_stream then
    while true do
      local frame, err = receiver:read_video_frame(video_stream)
      if not frame then assert(err == "empty" or err == "closed", err); break end
      assert(frame.width == width and frame.height == height and frame.format == "I420")
      assert(type(frame.metadata) == "table")
      if frame.metadata.frame_id then
        assert(frame.metadata.user_data == "lua")
      end
      assert(#frame.data == width * height * 3 / 2)
      video_frames = video_frames + 1
    end
  end
end
assert(audio_stream and video_stream, "receiver did not subscribe to both tracks")
for _, future in ipairs(subscription_futures) do assert(future:wait(50)) end
assert(audio_frames > 0 and video_frames > 0,
  string.format("expected decoded media; audio=%d video=%d", audio_frames, video_frames))
assert(local_subscribed[audio_sid] or local_subscribed[video_sid],
  "publisher did not observe a local-track subscription")
assert(type(publisher:local_track_rtc_stats(audio)) == "table")
assert(type(receiver:remote_track_rtc_stats("lua-publisher", audio_sid)) == "table")
local local_video_stats, remote_video_stats
for _ = 1, 60 do
  local_video_stats = assert(publisher:local_track_rtc_stats(video))
  remote_video_stats = assert(receiver:remote_track_rtc_stats("lua-publisher", video_sid))
  if #local_video_stats > 0 and #remote_video_stats > 0 then break end
  assert(receiver:step(25))
  assert(publisher:poll())
end
assert(#local_video_stats > 0 and #remote_video_stats > 0, "video RTC stats did not become ready")
assert(type(local_video_stats[1].id) == "string")
assert(type(local_video_stats[1].bytes) == "number")
assert(type(remote_video_stats[1].direction) == "number")
assert(publisher:clear_audio_source_queue(audio))
assert(assert(publisher:wait_audio_source_playout_async(audio, 1000)):wait(50))
assert(receiver:remote_stream_dropped_frames(audio_stream) >= 0)
assert(receiver:close_remote_stream(audio_stream))
assert(receiver:close_remote_stream(video_stream))
assert(assert(publisher:unpublish_local_track_async(audio)):wait(50))
assert(assert(publisher:unpublish_local_track_async(video)):wait(50))
assert(publisher:publish_audio_track("close-cleanup", 48000, 1, 200, {dtx = false}))
assert(publisher:close())
assert(receiver:disconnect())
assert(receiver:close())
print(string.format("received audio=%d video=%d", audio_frames, video_frames))
