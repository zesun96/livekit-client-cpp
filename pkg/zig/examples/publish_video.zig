const std = @import("std");
const livekit = @import("livekit");
const c = livekit.c;

pub fn main() !void {
    const url = std.c.getenv("LIVEKIT_URL") orelse return error.MissingLiveKitUrl;
    const token = std.c.getenv("LIVEKIT_TOKEN") orelse return error.MissingLiveKitToken;
    var threaded = std.Io.Threaded.init(std.heap.page_allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    try livekit.init();
    defer livekit.shutdown() catch {};
    var room = try livekit.Room.create();
    defer room.deinit();
    try room.connect(std.mem.span(url), std.mem.span(token));
    try room.waitConnected(io, 10000);

    var source_options = livekit.videoSourceOptions();
    var source = try livekit.VideoSource.create(&source_options);
    defer source.deinit() catch {};
    var track = try room.createVideoTrack("zig-synthetic-video", &source);
    defer track.deinit() catch {};
    defer room.disconnect() catch {};

    const width = 64;
    const height = 64;
    var frame: [width * height * 3 / 2]u8 = undefined;
    @memset(frame[0 .. width * height], 40);
    @memset(frame[width * height ..], 128);
    try source.captureI420(&frame, width, height, 0);

    var publish_options = livekit.trackPublishOptions();
    publish_options.source = c.LK_TRACK_SOURCE_CAMERA;
    try track.publish(&room, &publish_options);
    for (0..30) |index| {
        @memset(frame[0 .. width * height], @intCast(40 + index * 5));
        try source.captureI420(&frame, width, height, @intCast(index * 33333));
        try std.Io.sleep(io, .fromMilliseconds(33), .awake);
    }
    try track.unpublish(false);
    std.debug.print("published one second of synthetic I420 video\n", .{});
}
