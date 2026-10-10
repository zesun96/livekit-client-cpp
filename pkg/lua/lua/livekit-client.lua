local native = require("livekit_client_native")
local room_methods = native._room_methods
local native_poll = room_methods.poll
local native_close = room_methods.close
local waiting = {}
local futures_by_room = {}
local unpack_values = table.unpack or unpack
local function pack(...) return {n = select("#", ...), ...} end

local Future = {}
Future.__index = Future

local function future_for(room, id, err)
  if not id then return nil, err end
  local future = setmetatable({room = room, id = id}, Future)
  local futures = futures_by_room[room]
  if not futures then
    futures = {}
    futures_by_room[room] = futures
  end
  futures[id] = future
  return future
end

function Future:result()
  if self.ready then return true, self.ok, self.error end
  local ready, ok, err = self.room:_async_result(self.id)
  if ready == false then return false end
  self.ready = true
  if ready == nil then
    self.ok, self.error = nil, ok
  else
    self.ok, self.error = ok, err
  end
  local futures = futures_by_room[self.room]
  if futures then
    futures[self.id] = nil
    if not next(futures) then futures_by_room[self.room] = nil end
  end
  return true, self.ok, self.error
end

function Future:await()
  local ready, ok, err = self:result()
  if ready then return ok, err end
  local current, is_main = coroutine.running()
  if not current or is_main then error("await requires a coroutine; use future:wait() on the main thread", 2) end
  return coroutine.yield(self)
end

function Future:wait(timeout_ms)
  while true do
    local ready, ok, err = self:result()
    if ready then return ok, err end
    local count, poll_error = self.room:step(timeout_ms or 50)
    if not count then return nil, poll_error end
  end
end

local Task = {}
Task.__index = Task

function Task:done() return self.finished == true end

function Task:result()
  if not self.finished then return false end
  if self.error then return true, nil, self.error end
  return true, unpack_values(self.values, 1, self.values.n)
end

local function resume_task(task, ...)
  local response = pack(coroutine.resume(task.co, ...))
  if not response[1] then
    task.error = tostring(response[2])
    task.finished = true
    return
  end
  if coroutine.status(task.co) == "dead" then
    task.values = {n = response.n - 1}
    for i = 2, response.n do task.values[i - 1] = response[i] end
    task.finished = true
    return
  end
  local future = response[2]
  if getmetatable(future) ~= Future then
    task.error = "coroutine must yield a LiveKit future"
    task.finished = true
    return
  end
  task.waiting = future
  waiting[#waiting + 1] = task
end

function native.spawn(fn, ...)
  assert(type(fn) == "function", "spawn requires a function")
  local task = setmetatable({co = coroutine.create(fn)}, Task)
  resume_task(task, ...)
  return task
end

local function drive(room)
  for i = #waiting, 1, -1 do
    local task = waiting[i]
    if task.waiting.room == room then
      local ready, ok, err = task.waiting:result()
      if ready then
        table.remove(waiting, i)
        task.waiting = nil
        resume_task(task, ok, err)
      end
    end
  end
end

local function drain_futures(room)
  local futures = futures_by_room[room]
  if not futures then return end
  local snapshot = {}
  for _, future in pairs(futures) do snapshot[#snapshot + 1] = future end
  for _, future in ipairs(snapshot) do future:result() end
end

function room_methods:connect_async(url, token, options)
  return future_for(self, self:_start_connect(url, token, options))
end

function room_methods:connect_with_token_source_async(provider, fetch_options, connect_options)
  return future_for(self, self:_start_connect_token_source(provider, fetch_options, connect_options))
end

function room_methods:connect_with_token_source(provider, fetch_options, connect_options)
  return native.run(self, function()
    return assert(self:connect_with_token_source_async(provider, fetch_options, connect_options)):await()
  end)
end

function room_methods:disconnect_async()
  return future_for(self, self:_start_disconnect())
end

function room_methods:republish_all_tracks_async()
  return future_for(self, self:_start_republish_all_tracks())
end

function room_methods:publish_data_async(payload, reliable, topic)
  return future_for(self, self:_start_publish_data(payload, reliable, topic))
end

function room_methods:publish_data_with_options_async(payload, options)
  return future_for(self, self:_start_publish_data_with_options(payload, options or {}))
end

function room_methods:publish_audio_track_async(label, sample_rate, channels, queue_ms, publish_options)
  return future_for(self, self:_start_publish_audio_track(
    label, sample_rate, channels, queue_ms, publish_options))
end

function room_methods:publish_video_track_async(label, first_frame, width, height, format, screen,
                                                 publish_options)
  return future_for(self, self:_start_publish_video_track(
    label, first_frame, width, height, format, screen, publish_options))
end

function room_methods:wait_audio_source_playout_async(track_id, timeout_ms)
  return future_for(self, self:_start_wait_audio_source_playout(track_id, timeout_ms))
end

function room_methods:publish_capture_track_async(kind, label, options)
  return future_for(self, self:_start_publish_capture_track(kind, label, options))
end

function room_methods:publish_microphone_track(label, options)
  return self:publish_capture_track("microphone", label, options)
end

function room_methods:publish_microphone_track_async(label, options)
  return self:publish_capture_track_async("microphone", label, options)
end

function room_methods:publish_system_audio_track(label, options)
  return self:publish_capture_track("system_audio", label, options)
end

function room_methods:publish_system_audio_track_async(label, options)
  return self:publish_capture_track_async("system_audio", label, options)
end

function room_methods:publish_camera_track(label, options)
  return self:publish_capture_track("camera", label, options)
end

function room_methods:publish_camera_track_async(label, options)
  return self:publish_capture_track_async("camera", label, options)
end

function room_methods:publish_screen_track(label, options)
  return self:publish_capture_track("screen", label, options)
end

function room_methods:publish_screen_track_async(label, options)
  return self:publish_capture_track_async("screen", label, options)
end

function room_methods:start_capture(track_id)
  return self:control_capture(track_id, "start")
end

function room_methods:stop_capture(track_id)
  return self:control_capture(track_id, "stop")
end

function room_methods:switch_capture_source(track_id, source_id)
  return self:control_capture(track_id, "switch", source_id)
end

function room_methods:start_capture_async(track_id)
  return future_for(self, self:_start_control_capture(track_id, "start"))
end

function room_methods:stop_capture_async(track_id)
  return future_for(self, self:_start_control_capture(track_id, "stop"))
end

function room_methods:switch_capture_source_async(track_id, source_id)
  return future_for(self, self:_start_control_capture(track_id, "switch", source_id))
end

function room_methods:unpublish_local_track_async(track_id)
  return future_for(self, self:_start_unpublish_local_track(track_id))
end

function room_methods:set_remote_track_subscribed_async(participant_sid, track_sid, subscribed)
  return future_for(self, self:_start_set_remote_track_subscribed(participant_sid, track_sid, subscribed))
end

function room_methods:perform_rpc_async(destination, method, payload, timeout_ms)
  return future_for(self, self:_start_rpc(destination, method, payload, timeout_ms))
end

function room_methods:send_chat_message_async(message)
  return future_for(self, self:_start_chat(message))
end

function room_methods:send_text_async(value, topic)
  return future_for(self, self:_start_text(value, topic))
end

function room_methods:send_bytes_async(data, topic, mime_type, name)
  return future_for(self, self:_start_bytes(data, topic, mime_type, name))
end

function room_methods:send_file_async(path, topic, mime_type)
  return future_for(self, self:_start_file(path, topic, mime_type))
end

function room_methods:send_text_with_options_async(value, options)
  return future_for(self, self:_start_text_with_options(value, options or {}))
end

function room_methods:send_bytes_with_options_async(data, options)
  return future_for(self, self:_start_bytes_with_options(data, options or {}))
end

function room_methods:send_file_with_options_async(path, options)
  return future_for(self, self:_start_file_with_options(path, options or {}))
end

function room_methods:stream_text(options)
  return self:_open_stream_writer(options or {}, true)
end

function room_methods:stream_bytes(options)
  return self:_open_stream_writer(options or {}, false)
end

function room_methods:stream_text_async(options)
  return future_for(self, self:_start_open_stream_writer(options or {}, true))
end

function room_methods:stream_bytes_async(options)
  return future_for(self, self:_start_open_stream_writer(options or {}, false))
end

function room_methods:stream_writer_write_async(id, data)
  return future_for(self, self:_start_stream_writer_write(id, data))
end

function room_methods:stream_writer_close_async(id)
  return future_for(self, self:_start_stream_writer_close(id))
end

function room_methods:stream_writer_cancel_async(id, reason)
  return future_for(self, self:_start_stream_writer_cancel(id, reason))
end

function room_methods:store_data_track_schema_async(schema_id, definition)
  return future_for(self, self:_start_store_data_track_schema(schema_id, definition))
end

function room_methods:get_data_track_schema_async(identity, schema_id)
  return future_for(self, self:_start_get_data_track_schema(identity, schema_id))
end

function room_methods:publish_data_track_async(name, options)
  return future_for(self, self:_start_publish_data_track(name, options or {}))
end

function room_methods:subscribe_data_track_async(identity, sid, options)
  return future_for(self, self:_start_subscribe_data_track(identity, sid, options))
end

function room_methods:poll(max_events)
  local count, err = native_poll(self, max_events)
  if not count then return nil, err end
  drain_futures(self)
  drive(self)
  return count
end

function room_methods:step(timeout_ms, max_events)
  self:wait(timeout_ms or 50)
  return self:poll(max_events)
end

function room_methods:close()
  local ok, err = native_close(self)
  drain_futures(self)
  drive(self)
  return ok, err
end

function native.run(room, fn, ...)
  local task = native.spawn(fn, ...)
  while not task:done() do
    if task.waiting.room ~= room then return nil, "coroutine is waiting on another room" end
    local count, err = room:step(50)
    if not count then return nil, err end
  end
  return select(2, task:result())
end

return native
