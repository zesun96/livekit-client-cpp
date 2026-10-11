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
    defer room.disconnect() catch {};
    try room.waitConnected(io, 10000);

    var schema: c.lk_data_track_schema_id_t = undefined;
    c.lk_data_track_schema_id_init(&schema);
    schema.name = "zig.telemetry.v1";
    schema.encoding = c.LK_DATA_TRACK_SCHEMA_ENCODING_JSON_SCHEMA;
    room.storeDataTrackSchema(&schema, "{\"type\":\"object\",\"properties\":{\"temperature\":{\"type\":\"number\"}}") catch |err| {
        const message = try livekit.lastError(std.heap.page_allocator);
        defer std.heap.page_allocator.free(message);
        std.debug.print("store schema failed: {s}\n", .{message});
        return err;
    };

    var options = livekit.dataTrackPublishOptions();
    options.name = "zig-telemetry";
    options.has_frame_encoding = 1;
    options.frame_encoding = c.LK_DATA_TRACK_FRAME_ENCODING_JSON;
    options.has_schema = 1;
    options.schema = schema;
    var track = try room.publishDataTrack(&options);
    defer track.deinit() catch {};
    try track.push("{\"temperature\":21.5}", 1);
    try track.unpublish();
    std.debug.print("stored schema and published a DataTrack frame\n", .{});
}
