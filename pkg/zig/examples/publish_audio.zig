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

    var source_options = livekit.audioSourceOptions();
    source_options.sample_rate = 48000;
    source_options.num_channels = 1;
    source_options.queue_size_ms = 200;
    var source = try livekit.AudioSource.create(&source_options);
    defer source.deinit() catch {};
    var track = try room.createAudioTrack("zig-synthetic-tone", &source);
    defer track.deinit() catch {};
    defer room.disconnect() catch {};

    var publish_options = livekit.trackPublishOptions();
    publish_options.source = c.LK_TRACK_SOURCE_MICROPHONE;
    try track.publish(&room, &publish_options);

    var samples: [480]i16 = undefined;
    var sample_index: u64 = 0;
    for (0..100) |_| {
        for (&samples) |*sample| {
            const phase = 2.0 * std.math.pi * 440.0 * @as(f64, @floatFromInt(sample_index)) / 48000.0;
            sample.* = @intFromFloat(@sin(phase) * 6000.0);
            sample_index += 1;
        }
        try source.capture(&samples);
        try std.Io.sleep(io, .fromMilliseconds(10), .awake);
    }
    try track.unpublish(false);
    std.debug.print("published one second of synthetic 440 Hz audio\n", .{});
}
