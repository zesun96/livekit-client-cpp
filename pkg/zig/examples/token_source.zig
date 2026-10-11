const std = @import("std");
const livekit = @import("livekit");
const c = livekit.c;

const Credentials = struct {
    url: [*:0]const u8,
    token: [*:0]const u8,
};

fn provide(user_data: ?*anyopaque, _: ?*const c.lk_token_source_fetch_options_t, _: c_int, response_ptr: ?*c.lk_token_source_response_t) callconv(.c) c.lk_status_t {
    const credentials: *const Credentials = @ptrCast(@alignCast(user_data orelse return c.LK_STATUS_INVALID_ARGUMENT));
    const response = response_ptr orelse return c.LK_STATUS_INVALID_ARGUMENT;
    response.server_url = credentials.url;
    response.participant_token = credentials.token;
    return c.LK_STATUS_OK;
}

pub fn main() !void {
    const url = std.c.getenv("LIVEKIT_URL") orelse return error.MissingLiveKitUrl;
    const token = std.c.getenv("LIVEKIT_TOKEN") orelse return error.MissingLiveKitToken;
    var credentials = Credentials{ .url = url, .token = token };
    var threaded = std.Io.Threaded.init(std.heap.page_allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    try livekit.init();
    defer livekit.shutdown() catch {};
    var room = try livekit.Room.create();
    defer room.deinit();
    const fetch_options = livekit.tokenSourceFetchOptions();
    const connect_options = livekit.connectOptions();
    try room.connectWithTokenSource(provide, &credentials, &fetch_options, &connect_options);
    defer room.disconnect() catch {};

    try room.waitConnected(io, 10000);
    std.debug.print("connected with token source\n", .{});
}
