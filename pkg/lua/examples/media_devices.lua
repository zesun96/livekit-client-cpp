-- Mirrors examples/media_devices without opening any capture device.
local livekit = require("livekit-client")

local kinds = {[0] = "audio input", [1] = "audio output", [2] = "video input"}
local devices, err = livekit.list_media_devices()
assert(devices, err)
print("Found " .. #devices .. " media device(s)")
for _, device in ipairs(devices) do
  print(string.format("%s%s %s\n  %s", kinds[device.kind] or "unknown",
    device.is_default and " [default]" or "", device.label, device.id))
end
