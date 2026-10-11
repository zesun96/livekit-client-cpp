const std = @import("std");
const livekit = @import("livekit");

pub fn main() !void {
    const url = std.c.getenv("LIVEKIT_URL") orelse return error.MissingLiveKitUrl;
    const token = std.c.getenv("LIVEKIT_TOKEN") orelse return error.MissingLiveKitToken;
    const allocator = std.heap.page_allocator;
    var threaded = std.Io.Threaded.init(allocator, .{});
    defer threaded.deinit();

    try livekit.init();
    defer livekit.shutdown() catch {};
    var room = try livekit.Room.create();
    defer room.deinit();
    try room.connect(std.mem.span(url), std.mem.span(token));
    defer room.disconnect() catch {};
    try room.waitConnected(threaded.io(), 10000);

    var options = livekit.streamTextOptions();
    options.topic = "zig.incremental";
    options.has_total_size = 1;
    options.total_size = 11;
    var writer = try room.openTextStream(&options);
    defer writer.deinit();
    const id = try writer.id(allocator);
    defer allocator.free(id);
    try writer.write("hello");
    try writer.write(" ");
    try writer.write("world");
    try writer.close();
    std.debug.print("sent incremental text stream {s}\n", .{id});
}
