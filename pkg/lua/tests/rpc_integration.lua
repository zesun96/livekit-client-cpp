local livekit = require("livekit-client")
local url = assert(arg[1], "missing URL")
local receiver_token = assert(arg[2], "missing receiver token")
local caller_token = assert(arg[3], "missing caller token")

local receiver = assert(livekit.new_room())
local caller = assert(livekit.new_room())
local calls = 0
assert(receiver:register_rpc_method("example.echo", function(request)
  assert(type(request.request_id) == "string")
  assert(type(request.caller_identity) == "string")
  assert(type(request.response_timeout_ms) == "number")
  calls = calls + 1
  return "echo:" .. request.payload
end))
assert(receiver:register_rpc_method("example.fail", function()
  return {error_code = 1500, error_message = "expected failure", error_data = "detail"}
end))
assert(receiver:register_rpc_method("example.throw", function()
  error("Lua handler failure")
end))

assert(receiver:connect(url, receiver_token))
assert(caller:connect(url, caller_token))
local destination = receiver:local_identity()

local function call(method, payload)
  local future = assert(caller:perform_rpc_async(destination, method, payload, 5000))
  for _ = 1, 200 do
    assert(receiver:step(20))
    assert(caller:step(20))
    local ready, result, err = future:result()
    if ready then return assert(result, err) end
  end
  error("RPC did not finish")
end

local echo = call("example.echo", "hello")
assert(echo.ok and echo.payload == "echo:hello")
assert(calls == 1)
local failure = call("example.fail", "")
assert(not failure.ok and failure.error_code == 1500)
assert(failure.error_message == "expected failure" and failure.error_data == "detail")
local thrown = call("example.throw", "")
assert(not thrown.ok and thrown.error_code == 1500)
assert(thrown.error_message:find("Lua handler failure", 1, true))

assert(receiver:unregister_rpc_method("example.echo"))
local missing = call("example.echo", "hello")
assert(not missing.ok and missing.error_code == 1400)
assert(receiver:register_rpc_method("example.pending", function() return "unexpected" end))
local pending = assert(caller:perform_rpc_async(destination, "example.pending", "", 1000))
assert(caller:step(50))
assert(receiver:close())
local pending_result, pending_error = pending:wait(50)
assert(pending_result == nil or not pending_result.ok, pending_error)
assert(caller:disconnect())
assert(caller:close())
