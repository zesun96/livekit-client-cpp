-- Send an incremental text stream. The receiver gets a text_received event after close.
local livekit = require("livekit-client")
local url = arg[1] or os.getenv("LIVEKIT_URL")
local token = arg[2] or os.getenv("LIVEKIT_TOKEN")
assert(url and token, "usage: stream_writer.lua <url> <token>")

local room = assert(livekit.new_room())
assert(room:connect(url, token))
local writer = assert(room:stream_text({topic = "lua-incremental", attributes = {source = "lua"}}))
local info = assert(room:stream_writer_info(writer))
print("stream ID", info.stream_id)
for _, chunk in ipairs({"hello", " ", "world"}) do
  assert(room:stream_writer_write(writer, chunk))
end
assert(room:stream_writer_close(writer))
assert(room:stream_writer_release(writer))
assert(room:disconnect())
assert(room:close())
