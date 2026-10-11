const std = @import("std");
const livekit = @import("livekit");

pub fn main() !void {
    const url = std.c.getenv("LIVEKIT_URL") orelse return error.MissingLiveKitUrl;
    const token = std.c.getenv("LIVEKIT_TOKEN") orelse return error.MissingLiveKitToken;
    const key_env = std.c.getenv("LIVEKIT_E2EE_KEY") orelse return error.MissingE2eeKey;
    const key = std.mem.span(key_env);
    if (key.len < 32) return error.E2eeKeyTooShort;
    var threaded = std.Io.Threaded.init(std.heap.page_allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    try livekit.init();
    defer livekit.shutdown() catch {};
    var room = try livekit.Room.create();
    defer room.deinit();
    var encryption = livekit.e2eeOptions();
    encryption.shared_key = key.ptr;
    encryption.shared_key_size = key.len;
    var options = livekit.connectOptions();
    options.e2ee_options = &encryption;
    try room.connectWithOptions(std.mem.span(url), std.mem.span(token), &options);
    defer room.disconnect() catch {};

    try room.waitConnected(io, 10000);
    if (!room.e2eeConfigured() or !room.e2eeEnabled()) return error.E2eeNotEnabled;
    try room.setSharedKey(key, 0);
    std.debug.print("connected with E2EE and installed shared key\n", .{});
}
