const std = @import("std");
const livekit = @import("livekit");
const c = livekit.c;

pub fn main() !void {
    const url = std.c.getenv("LIVEKIT_URL") orelse return error.MissingLiveKitUrl;
    const token = std.c.getenv("LIVEKIT_TOKEN") orelse return error.MissingLiveKitToken;
    var threaded = std.Io.Threaded.init(std.heap.page_allocator, .{});
    defer threaded.deinit();

    try livekit.init();
    defer livekit.shutdown() catch {};
    var room = try livekit.Room.create();
    defer room.deinit();
    try room.connect(std.mem.span(url), std.mem.span(token));
    defer room.disconnect() catch {};
    try room.waitConnected(threaded.io(), 10000);

    var data_options = livekit.dataPublishOptions();
    data_options.topic = "zig.example";
    try room.publishData("hello-data", &data_options);

    var text_options: c.lk_text_send_options_t = undefined;
    c.lk_text_send_options_init(&text_options);
    text_options.topic = "zig.example";
    try room.sendText("hello-text", &text_options);

    var byte_options: c.lk_byte_send_options_t = undefined;
    c.lk_byte_send_options_init(&byte_options);
    byte_options.topic = "zig.example";
    byte_options.mime_type = "application/octet-stream";
    byte_options.name = "example.bin";
    try room.sendBytes(&.{ 0, 1, 0, 255 }, &byte_options);

    var message_id: [128]u8 = undefined;
    var timestamp: i64 = 0;
    try livekit.check(c.lk_room_send_chat_message(room.handle, "hello-chat", &message_id, message_id.len, &timestamp));
    std.debug.print("sent data, text, bytes, chat (chat id={s}, timestamp={d})\n", .{ std.mem.sliceTo(&message_id, 0), timestamp });
}
