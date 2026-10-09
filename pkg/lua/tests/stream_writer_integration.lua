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
local received = {}
assert(receiver:on(function(event)
  if event.type == "text_received" or event.type == "byte_received" then
    received[event.topic] = event.text or event.data
  end
end))
assert(receiver:connect(url, receiver_token))
assert(publisher:connect(url, publisher_token))

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

for _ = 1, 200 do
  assert(receiver:step(25))
  assert(publisher:poll())
  if received["lua-stream-text"] and received["lua-stream-bytes"] then break end
end
assert(received["lua-stream-text"] == "hello world")
assert(received["lua-stream-bytes"] == "\0\1\2\3")
assert(publisher:disconnect())
assert(receiver:disconnect())
assert(publisher:close())
assert(receiver:close())
print("incremental streams delivered")
