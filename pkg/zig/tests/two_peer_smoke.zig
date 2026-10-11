const std = @import("std");
const livekit = @import("livekit");
const c = livekit.c;

const payload = "zig-two-peer-data";
const topic = "zig.smoke";

const State = struct {
    joined: std.atomic.Value(bool) = .init(false),
    received: std.atomic.Value(bool) = .init(false),
    chat: std.atomic.Value(bool) = .init(false),
    text: std.atomic.Value(bool) = .init(false),
    bytes: std.atomic.Value(bool) = .init(false),
    expected_identity: []const u8 = "",
};

fn onParticipant(user_data: ?*anyopaque, _: ?*c.lk_room_t, _: ?*const c.lk_participant_info_t) callconv(.c) void {
    const state: *State = @ptrCast(@alignCast(user_data orelse return));
    state.joined.store(true, .release);
}

fn onData(user_data: ?*anyopaque, _: ?*c.lk_room_t, event_ptr: ?*const c.lk_data_received_t) callconv(.c) void {
    const state: *State = @ptrCast(@alignCast(user_data orelse return));
    const event = event_ptr orelse return;
    if (event.data == null or event.topic == null or event.participant_identity == null) return;
    if (!std.mem.eql(u8, event.data[0..event.data_size], payload)) return;
    if (!std.mem.eql(u8, std.mem.span(event.topic), topic)) return;
    if (!std.mem.eql(u8, std.mem.span(event.participant_identity), state.expected_identity)) return;
    state.received.store(true, .release);
}

fn onChat(user_data: ?*anyopaque, _: ?*c.lk_room_t, event_ptr: ?*const c.lk_chat_message_t) callconv(.c) void {
    const state: *State = @ptrCast(@alignCast(user_data orelse return));
    const event = event_ptr orelse return;
    if (event.message == null or event.participant_identity == null) return;
    if (!std.mem.eql(u8, std.mem.span(event.message), "hello-chat")) return;
    if (!std.mem.eql(u8, std.mem.span(event.participant_identity), state.expected_identity)) return;
    state.chat.store(true, .release);
}

fn onText(user_data: ?*anyopaque, _: ?*c.lk_room_t, event_ptr: ?*const c.lk_text_stream_event_t) callconv(.c) void {
    const state: *State = @ptrCast(@alignCast(user_data orelse return));
    const event = event_ptr orelse return;
    if (event.type != c.LK_DATA_STREAM_EVENT_CHUNK or event.content == null) return;
    if (std.mem.eql(u8, event.content[0..event.content_size], "hello-text")) state.text.store(true, .release);
}

fn onBytes(user_data: ?*anyopaque, _: ?*c.lk_room_t, event_ptr: ?*const c.lk_byte_stream_event_t) callconv(.c) void {
    const state: *State = @ptrCast(@alignCast(user_data orelse return));
    const event = event_ptr orelse return;
    if (event.type != c.LK_DATA_STREAM_EVENT_CHUNK or event.content == null) return;
    if (std.mem.eql(u8, event.content[0..event.content_size], &.{ 0, 1, 0, 255 })) state.bytes.store(true, .release);
}

fn onRpc(_: ?*anyopaque, _: ?*const c.lk_rpc_invocation_t, result: ?*c.lk_rpc_handler_result_t) callconv(.c) void {
    const out = result orelse return;
    out.* = .{
        .payload = "hello-rpc!",
        .error_code = 0,
        .error_message = null,
        .error_data = null,
    };
}

fn waitFor(io: std.Io, flag: *const std.atomic.Value(bool)) !void {
    const deadline = std.Io.Clock.awake.now(io).nanoseconds + 15 * std.time.ns_per_s;
    while (!flag.load(.acquire)) {
        if (std.Io.Clock.awake.now(io).nanoseconds >= deadline) return error.TimedOut;
        try std.Io.sleep(io, .fromMilliseconds(10), .awake);
    }
}

pub fn main() !void {
    const url = std.c.getenv("LIVEKIT_URL") orelse return error.MissingLiveKitUrl;
    const token_a = std.c.getenv("LIVEKIT_TOKEN_A") orelse return error.MissingLiveKitTokenA;
    const token_b = std.c.getenv("LIVEKIT_TOKEN_B") orelse return error.MissingLiveKitTokenB;
    const allocator = std.heap.page_allocator;
    var threaded = std.Io.Threaded.init(allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    try livekit.init();
    defer livekit.shutdown() catch {};

    var state = State{};
    var sender = try livekit.Room.create();
    defer sender.deinit();
    var sender_callbacks = livekit.roomCallbacks();
    sender_callbacks.user_data = &state;
    sender_callbacks.on_participant_connected = onParticipant;
    try sender.setCallbacks(&sender_callbacks);
    try sender.connect(std.mem.span(url), std.mem.span(token_a));
    defer sender.disconnect() catch {};
    try sender.waitConnected(io, 10000);

    const sender_identity = try sender.localIdentity(allocator);
    defer allocator.free(sender_identity);
    state.expected_identity = sender_identity;

    var receiver = try livekit.Room.create();
    defer receiver.deinit();
    var receiver_callbacks = livekit.roomCallbacks();
    receiver_callbacks.user_data = &state;
    receiver_callbacks.on_data_received = onData;
    receiver_callbacks.on_chat_message_received = onChat;
    try receiver.setCallbacks(&receiver_callbacks);
    try livekit.check(c.lk_room_register_text_stream_handler(receiver.handle, topic, onText, &state));
    try livekit.check(c.lk_room_register_byte_stream_handler(receiver.handle, topic, onBytes, &state));
    try livekit.check(c.lk_room_register_rpc_method_out(receiver.handle, "zig.echo", onRpc, null));
    try receiver.connect(std.mem.span(url), std.mem.span(token_b));
    defer receiver.disconnect() catch {};
    try receiver.waitConnected(io, 10000);

    const receiver_identity = try receiver.localIdentity(allocator);
    defer allocator.free(receiver_identity);
    if (std.mem.eql(u8, sender_identity, receiver_identity)) return error.DuplicateIdentity;
    try waitFor(io, &state.joined);

    var options = livekit.dataPublishOptions();
    options.topic = topic;
    try sender.publishData(payload, &options);
    try waitFor(io, &state.received);

    var chat_id: [128]u8 = undefined;
    var chat_timestamp: i64 = 0;
    try livekit.check(c.lk_room_send_chat_message(sender.handle, "hello-chat", &chat_id, chat_id.len, &chat_timestamp));
    try waitFor(io, &state.chat);

    var text_options: c.lk_text_send_options_t = undefined;
    c.lk_text_send_options_init(&text_options);
    text_options.topic = topic;
    try sender.sendText("hello-text", &text_options);
    try waitFor(io, &state.text);

    var byte_options = livekit.streamBytesOptions();
    byte_options.topic = topic;
    byte_options.mime_type = "application/octet-stream";
    var byte_writer = try sender.openByteStream(&byte_options);
    defer byte_writer.deinit();
    const byte_stream_id = try byte_writer.id(allocator);
    defer allocator.free(byte_stream_id);
    try byte_writer.write(&.{ 0, 1, 0, 255 });
    try byte_writer.close();
    try waitFor(io, &state.bytes);

    const receiver_identity_z = try allocator.dupeSentinel(u8, receiver_identity, 0);
    defer allocator.free(receiver_identity_z);
    var rpc_options = livekit.rpcOptions();
    rpc_options.destination_identity = receiver_identity_z.ptr;
    rpc_options.method = "zig.echo";
    rpc_options.payload = "hello-rpc";
    var rpc_result = try sender.performRpc(&rpc_options);
    defer rpc_result.deinit();
    if (!rpc_result.ok()) return error.RpcFailed;
    const reply = try rpc_result.payload(allocator);
    defer allocator.free(reply);
    if (!std.mem.eql(u8, reply, "hello-rpc!")) return error.UnexpectedRpcReply;

    std.debug.print("PASS two peers: connect, data, chat, text/byte streams, RPC\n", .{});
}
