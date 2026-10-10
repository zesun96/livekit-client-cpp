-- Opt-in incremental stream writer test against a local LiveKit server.
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
local chunks, closed, received, received_events = {}, {}, {}, {}
local progress, completions = {}, {}
assert(publisher:on(function(event)
  if event.type == "stream_writer_progress" then
    assert(type(event.writer_id) == "number" and type(event.bytes_sent) == "number")
    assert(type(event.has_total_size) == "boolean")
    progress[event.writer_id] = event
  elseif event.type == "stream_writer_complete" then
    assert(type(event.stream_id) == "string" and event.stream_id ~= "")
    assert(type(event.reason) == "string" and type(event.error_code) == "number")
    assert(type(event.error_domain) == "number")
    assert(not completions[event.writer_id], "completion was delivered more than once")
    completions[event.writer_id] = event
  end
end))
assert(receiver:on(function(event)
  if event.type == "text_stream_event" or event.type == "byte_stream_event" then
    if event.phase == 1 then
      chunks[event.topic] = (chunks[event.topic] or "") .. event.content
    elseif event.phase == 2 then
      closed[event.topic] = true
    end
  elseif event.type == "text_received" then
    received[event.topic] = event.text
    received_events[event.topic] = event
  elseif event.type == "byte_received" or event.type == "file_received" or
      event.type == "data_received" then
    received[event.topic] = event.data
    received_events[event.topic] = event
  end
end))
assert(receiver:register_text_stream_handler("lua-stream-text"))
assert(receiver:register_byte_stream_handler("lua-stream-bytes"))
assert(receiver:connect(url, receiver_token))
assert(publisher:connect(url, publisher_token))
assert(publisher:republish_all_tracks())
assert(assert(publisher:republish_all_tracks_async()):wait(50))

local text_writer = assert(publisher:stream_text({
  topic = "lua-stream-text", attributes = {origin = "lua"}, total_size = 11
}))
local text_info = assert(publisher:stream_writer_info(text_writer))
assert(text_info.topic == "lua-stream-text" and text_info.attributes.origin == "lua")
assert(text_info.has_total_size and text_info.total_size == 11)
assert(publisher:stream_writer_write(text_writer, "hello "))
assert(assert(publisher:stream_writer_write_async(text_writer, "world")):wait(50))
assert(assert(publisher:stream_writer_close_async(text_writer)):wait(50))
assert(publisher:stream_writer_info(text_writer).is_closed)
assert(publisher:stream_writer_release(text_writer))

local byte_writer = assert(assert(publisher:stream_bytes_async({
  topic = "lua-stream-bytes", mime_type = "application/octet-stream", name = "sample.bin"
})):wait(50))
assert(publisher:stream_writer_write(byte_writer, "\0\1"))
assert(publisher:stream_writer_write(byte_writer, "\2\3"))
assert(publisher:stream_writer_close(byte_writer))
assert(publisher:stream_writer_release(byte_writer))

local cancelled_writer = assert(publisher:stream_text({topic = "lua-stream-cancel"}))
assert(publisher:stream_writer_write(cancelled_writer, "discard"))
assert(assert(publisher:stream_writer_cancel_async(cancelled_writer, "test cancellation")):wait(50))
assert(publisher:stream_writer_info(cancelled_writer).is_closed)
assert(publisher:stream_writer_release(cancelled_writer))
local abandoned_writer = assert(publisher:stream_bytes({topic = "lua-stream-abandoned"}))
assert(publisher:stream_writer_release(abandoned_writer))
local incomplete_writer = assert(publisher:stream_text({
  topic = "lua-stream-incomplete", total_size = 4
}))
assert(publisher:stream_writer_write(incomplete_writer, "abc"))
local incomplete_ok, incomplete_error = publisher:stream_writer_close(incomplete_writer)
assert(incomplete_ok == nil and type(incomplete_error) == "string")
assert(publisher:stream_writer_release(incomplete_writer))
assert(publisher:poll())
assert(progress[text_writer] and progress[text_writer].bytes_sent == 11)
assert(progress[text_writer].has_total_size and progress[text_writer].total_size == 11)
assert(completions[text_writer].status == livekit.STREAM_COMPLETION.COMPLETED)
assert(completions[text_writer].bytes_sent == 11)
assert(completions[text_writer].has_total_size and completions[text_writer].total_size == 11)
assert(progress[byte_writer] and progress[byte_writer].bytes_sent == 4)
assert(not progress[byte_writer].has_total_size and progress[byte_writer].total_size == nil)
assert(completions[byte_writer].status == livekit.STREAM_COMPLETION.COMPLETED)
assert(completions[byte_writer].bytes_sent == 4)
assert(completions[cancelled_writer].status == livekit.STREAM_COMPLETION.CANCELLED)
assert(completions[cancelled_writer].reason == "test cancellation")
assert(completions[abandoned_writer].status == livekit.STREAM_COMPLETION.CANCELLED)
assert(completions[abandoned_writer].reason == "writer destroyed before close")
assert(completions[incomplete_writer].status == livekit.STREAM_COMPLETION.FAILED)
assert(completions[incomplete_writer].bytes_sent == 3)
assert(completions[incomplete_writer].error_domain == livekit.ERROR_DOMAIN.STATUS)
assert(completions[incomplete_writer].error_code ~= 0)

assert(publisher:send_text_with_options("directed text", {
  topic = "lua-one-shot-text", destination_identities = {"receiver"},
  attributes = {origin = "lua"}, compress = true, chunk_size = 1024
}))
assert(assert(publisher:send_bytes_with_options_async("\0\5", {
  topic = "lua-one-shot-bytes", destination_identities = {"receiver"},
  mime_type = "application/octet-stream", name = "bytes.bin", compress = true,
  attributes = {origin = "lua"}
})):wait(50))
local file_path = os.tmpname()
local file = assert(io.open(file_path, "wb"))
assert(file:write("file\0data"))
file:close()
assert(assert(publisher:send_file_with_options_async(file_path, {
  topic = "lua-one-shot-file", destination_identities = {"receiver"},
  mime_type = "application/octet-stream", compress = true, attributes = {origin = "lua"}
})):wait(50))
os.remove(file_path)
assert(assert(publisher:publish_data_with_options_async("data\0message", {
  topic = "lua-directed-data", destination_identities = {"receiver"}, reliable = true
})):wait(50))

for _ = 1, 200 do
  assert(receiver:step(25))
  assert(publisher:poll())
  if closed["lua-stream-text"] and closed["lua-stream-bytes"] and
      received["lua-one-shot-text"] and received["lua-one-shot-bytes"] and
      received["lua-one-shot-file"] and received["lua-directed-data"] then break end
end
assert(chunks["lua-stream-text"] == "hello world" and closed["lua-stream-text"])
assert(chunks["lua-stream-bytes"] == "\0\1\2\3" and closed["lua-stream-bytes"])
assert(received["lua-one-shot-text"] == "directed text")
assert(received_events["lua-one-shot-text"].attributes.origin == "lua")
assert(type(received_events["lua-one-shot-text"].attached_stream_ids) == "table")
assert(received["lua-one-shot-bytes"] == "\0\5")
assert(received_events["lua-one-shot-bytes"].attributes.origin == "lua")
assert(received["lua-one-shot-file"] == "file\0data")
assert(received_events["lua-one-shot-file"].attributes.origin == "lua")
assert(received["lua-directed-data"] == "data\0message")
assert(receiver:unregister_text_stream_handler("lua-stream-text"))
assert(receiver:unregister_byte_stream_handler("lua-stream-bytes"))
assert(publisher:disconnect())
assert(receiver:disconnect())
assert(publisher:close())
assert(receiver:close())
print("incremental streams delivered")
