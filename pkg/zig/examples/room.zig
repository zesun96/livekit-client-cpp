const std = @import("std");
const livekit = @import("livekit");

fn onConnected(_: ?*anyopaque, _: ?*livekit.c.lk_room_t) callconv(.c) void {
    std.debug.print("connected\n", .{});
}

fn onData(_: ?*anyopaque, _: ?*livekit.c.lk_room_t, event: ?*const livekit.c.lk_data_received_t) callconv(.c) void {
    const data = event orelse return;
    // Borrowed payload: copy it here if it must survive this callback.
    std.debug.print("received {d} bytes\n", .{data.data_size});
}

pub fn main() !void {
    const allocator = std.heap.page_allocator;
    var threaded = std.Io.Threaded.init(allocator, .{});
    defer threaded.deinit();
    const url = std.c.getenv("LIVEKIT_URL") orelse return error.MissingLiveKitUrl;
    const token = std.c.getenv("LIVEKIT_TOKEN") orelse return error.MissingLiveKitToken;

    try livekit.init();
    defer livekit.shutdown() catch {};
    var room = try livekit.Room.create();
    defer room.deinit();

    var callbacks = livekit.roomCallbacks();
    callbacks.on_connected = onConnected;
    callbacks.on_data_received = onData;
    try room.setCallbacks(&callbacks);

    room.connect(std.mem.span(url), std.mem.span(token)) catch |err| {
        const message = try livekit.lastError(allocator);
        defer allocator.free(message);
        std.debug.print("connect failed: {s}\n", .{message});
        return err;
    };
    defer room.disconnect() catch {};
    try room.waitConnected(threaded.io(), 10000);

    const identity = try room.localIdentity(allocator);
    defer allocator.free(identity);
    std.debug.print("local identity: {s}\n", .{identity});
    try room.publishData("hello from Zig", null);
}
