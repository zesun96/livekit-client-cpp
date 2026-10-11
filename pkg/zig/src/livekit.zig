const std = @import("std");

/// Complete, unmodified LiveKit C ABI. Advanced features (media, streams, RPC,
/// E2EE, DataTrack, statistics) are available here with their native signatures.
pub const c = @import("livekit_c");

pub const Error = error{ NativeFailure, OutOfMemory, InvalidArgument };

/// The native error belongs to the calling thread and can be replaced by the
/// next C API call. Copy it immediately after a failed operation.
pub fn lastError(allocator: std.mem.Allocator) Error![]u8 {
    const message = c.lk_last_error();
    if (message == null) return allocator.dupe(u8, "unknown native error") catch error.OutOfMemory;
    return allocator.dupe(u8, std.mem.span(message)) catch error.OutOfMemory;
}

pub fn check(status: c.lk_status_t) Error!void {
    if (status != c.LK_STATUS_OK) return error.NativeFailure;
}

pub fn checkDataTrack(status: c.lk_data_track_error_code_t) Error!void {
    if (status != c.LK_DATA_TRACK_ERROR_NONE) return error.NativeFailure;
}

pub fn init() Error!void {
    try check(c.lk_init());
}

/// Call after destroying all rooms, tracks, sources, streams, and snapshots.
pub fn shutdown() Error!void {
    try check(c.lk_shutdown());
}

pub fn version(allocator: std.mem.Allocator) Error![]u8 {
    return readString(allocator, c.lk_version, .{});
}

/// Read a C ABI string getter, which returns the required size including NUL.
/// `args` contains any arguments before the buffer and size parameters.
pub fn readString(allocator: std.mem.Allocator, getter: anytype, args: anytype) Error![]u8 {
    const needed = @call(.auto, getter, args ++ .{ null, @as(usize, 0) });
    if (needed == 0) return error.NativeFailure;
    const buffer = allocator.alloc(u8, needed) catch return error.OutOfMemory;
    errdefer allocator.free(buffer);
    const written = @call(.auto, getter, args ++ .{ buffer.ptr, buffer.len });
    if (written == 0 or written > buffer.len) return error.NativeFailure;
    return allocator.realloc(buffer, written - 1) catch return error.OutOfMemory;
}

/// Room owns one native handle. Keep callbacks' user_data alive until deinit.
/// Deinitialize only after disconnect completes and never from a callback.
pub const Room = struct {
    handle: *c.lk_room_t,

    pub fn create() Error!Room {
        var handle: ?*c.lk_room_t = null;
        try check(c.lk_room_create(&handle));
        return .{ .handle = handle orelse return error.NativeFailure };
    }

    pub fn deinit(self: *Room) void {
        c.lk_room_destroy(self.handle);
        self.* = undefined;
    }

    pub fn setCallbacks(self: *Room, callbacks: *const c.lk_room_callbacks_t) Error!void {
        try check(c.lk_room_set_callbacks(self.handle, callbacks));
    }

    pub fn connect(self: *Room, url: [:0]const u8, token: [:0]const u8) Error!void {
        try check(c.lk_room_connect(self.handle, url.ptr, token.ptr));
    }

    pub fn connectWithOptions(self: *Room, url: [:0]const u8, token: [:0]const u8, options: *const c.lk_room_connect_options_t) Error!void {
        try check(c.lk_room_connect_with_options(self.handle, url.ptr, token.ptr, options));
    }

    /// The callback and user_data must remain valid through disconnect.
    pub fn connectWithTokenSource(self: *Room, callback: c.lk_token_source_callback, user_data: ?*anyopaque, fetch_options: *const c.lk_token_source_fetch_options_t, options: *const c.lk_room_connect_options_t) Error!void {
        try check(c.lk_room_connect_with_token_source_and_options(self.handle, callback, user_data, fetch_options, options));
    }

    pub fn disconnect(self: *Room) Error!void {
        try check(c.lk_room_disconnect(self.handle));
    }

    pub fn isConnected(self: *const Room) bool {
        return c.lk_room_is_connected(self.handle) != 0;
    }

    /// Wait for the room to reach CONNECTED after the native connect call.
    pub fn waitConnected(self: *const Room, io: std.Io, timeout_ms: u32) !void {
        const deadline = std.Io.Clock.awake.now(io).nanoseconds + @as(i96, timeout_ms) * std.time.ns_per_ms;
        while (!self.isConnected()) {
            if (std.Io.Clock.awake.now(io).nanoseconds >= deadline) return error.TimedOut;
            try std.Io.sleep(io, .fromMilliseconds(10), .awake);
        }
    }

    pub fn state(self: *const Room) c.lk_room_state_t {
        return c.lk_room_state(self.handle);
    }

    pub fn sid(self: *const Room, allocator: std.mem.Allocator) Error![]u8 {
        return readString(allocator, c.lk_room_sid, .{self.handle});
    }

    pub fn name(self: *const Room, allocator: std.mem.Allocator) Error![]u8 {
        return readString(allocator, c.lk_room_name, .{self.handle});
    }

    pub fn metadata(self: *const Room, allocator: std.mem.Allocator) Error![]u8 {
        return readString(allocator, c.lk_room_metadata, .{self.handle});
    }

    pub fn localIdentity(self: *const Room, allocator: std.mem.Allocator) Error![]u8 {
        return readString(allocator, c.lk_local_participant_identity, .{self.handle});
    }

    pub fn localSnapshot(self: *const Room) Error!LocalSnapshot {
        var snapshot: ?*c.lk_local_participant_snapshot_t = null;
        try check(c.lk_room_create_local_participant_snapshot(self.handle, &snapshot));
        return .{ .handle = snapshot orelse return error.NativeFailure };
    }

    pub fn remoteParticipants(self: *const Room) Error!RemoteParticipants {
        var snapshot: ?*c.lk_remote_participant_list_t = null;
        try check(c.lk_room_create_remote_participant_snapshot(self.handle, &snapshot));
        return .{ .handle = snapshot orelse return error.NativeFailure };
    }

    pub fn setLocalName(self: *Room, name_value: [:0]const u8) Error!void {
        try check(c.lk_local_participant_set_name(self.handle, name_value.ptr));
    }

    pub fn setLocalMetadata(self: *Room, value: [:0]const u8) Error!void {
        try check(c.lk_local_participant_set_metadata(self.handle, value.ptr));
    }

    pub fn e2eeConfigured(self: *const Room) bool {
        return c.lk_room_e2ee_is_configured(self.handle) != 0;
    }

    pub fn e2eeEnabled(self: *const Room) bool {
        return c.lk_room_e2ee_is_enabled(self.handle) != 0;
    }

    pub fn setE2eeEnabled(self: *Room, enabled: bool) Error!void {
        try check(c.lk_room_e2ee_set_enabled(self.handle, @intFromBool(enabled)));
    }

    pub fn setSharedKey(self: *Room, key: []const u8, index: usize) Error!void {
        try check(c.lk_room_e2ee_set_shared_key(self.handle, key.ptr, key.len, index));
    }

    pub fn publishData(self: *Room, bytes: []const u8, options: ?*const c.lk_data_publish_options_t) Error!void {
        try check(c.lk_room_publish_data(self.handle, bytes.ptr, bytes.len, options));
    }

    pub fn sendText(self: *Room, text: [:0]const u8, options: ?*const c.lk_text_send_options_t) Error!void {
        try check(c.lk_room_send_text(self.handle, text.ptr, options));
    }

    pub fn sendBytes(self: *Room, bytes: []const u8, options: ?*const c.lk_byte_send_options_t) Error!void {
        try check(c.lk_room_send_bytes(self.handle, bytes.ptr, bytes.len, options));
    }

    pub fn sendFile(self: *Room, path: [:0]const u8, options: ?*const c.lk_file_send_options_t) Error!void {
        try check(c.lk_room_send_file(self.handle, path.ptr, options));
    }

    pub fn openTextStream(self: *Room, options: *const c.lk_stream_text_options_t) Error!TextWriter {
        var writer: ?*c.lk_text_stream_writer_t = null;
        try check(c.lk_room_stream_text(self.handle, options, &writer));
        return .{ .handle = writer orelse return error.NativeFailure };
    }

    pub fn openByteStream(self: *Room, options: *const c.lk_stream_bytes_options_t) Error!ByteWriter {
        var writer: ?*c.lk_byte_stream_writer_t = null;
        try check(c.lk_room_stream_bytes(self.handle, options, &writer));
        return .{ .handle = writer orelse return error.NativeFailure };
    }

    pub fn performRpc(self: *Room, options: *const c.lk_rpc_perform_options_t) Error!RpcResult {
        var result: ?*c.lk_rpc_result_t = null;
        try check(c.lk_room_perform_rpc(self.handle, options, &result));
        return .{ .handle = result orelse return error.NativeFailure };
    }

    pub fn storeDataTrackSchema(self: *Room, schema: *const c.lk_data_track_schema_id_t, definition: []const u8) Error!void {
        try checkDataTrack(c.lk_room_store_data_track_schema(self.handle, schema, definition.ptr, definition.len));
    }

    pub fn publishDataTrack(self: *Room, options: *const c.lk_data_track_publish_options_t) Error!DataTrack {
        var track: ?*c.lk_local_data_track_t = null;
        try checkDataTrack(c.lk_room_publish_data_track(self.handle, options, &track));
        return .{ .handle = track orelse return error.NativeFailure };
    }

    pub fn createAudioTrack(self: *Room, label: [:0]const u8, source: *const AudioSource) Error!LocalTrack {
        var track: ?*c.lk_local_track_t = null;
        try check(c.lk_room_create_audio_track(self.handle, label.ptr, source.handle, &track));
        return .{ .handle = track orelse return error.NativeFailure };
    }

    pub fn createVideoTrack(self: *Room, label: [:0]const u8, source: *const VideoSource) Error!LocalTrack {
        var track: ?*c.lk_local_track_t = null;
        try check(c.lk_room_create_video_track(self.handle, label.ptr, source.handle, &track));
        return .{ .handle = track orelse return error.NativeFailure };
    }
};

pub const LocalSnapshot = struct {
    handle: *c.lk_local_participant_snapshot_t,

    pub fn deinit(self: *LocalSnapshot) void {
        c.lk_local_participant_snapshot_destroy(self.handle);
        self.* = undefined;
    }

    pub fn identity(self: *const LocalSnapshot, allocator: std.mem.Allocator) Error![]u8 {
        return readString(allocator, c.lk_local_participant_snapshot_identity, .{self.handle});
    }
};

pub const RemoteParticipants = struct {
    handle: *c.lk_remote_participant_list_t,

    pub fn deinit(self: *RemoteParticipants) void {
        c.lk_remote_participant_list_destroy(self.handle);
        self.* = undefined;
    }

    pub fn count(self: *const RemoteParticipants) usize {
        return c.lk_remote_participant_list_count(self.handle);
    }

    /// The returned participant belongs to this snapshot and must not be destroyed.
    pub fn at(self: *const RemoteParticipants, index: usize) Error!*const c.lk_remote_participant_snapshot_t {
        var participant: ?*const c.lk_remote_participant_snapshot_t = null;
        try check(c.lk_remote_participant_list_at(self.handle, index, &participant));
        return participant orelse error.NativeFailure;
    }
};

pub const TextWriter = struct {
    handle: *c.lk_text_stream_writer_t,

    pub fn deinit(self: *TextWriter) void {
        c.lk_text_stream_writer_destroy(self.handle);
        self.* = undefined;
    }

    pub fn write(self: *TextWriter, text_value: []const u8) Error!void {
        try check(c.lk_text_stream_writer_write(self.handle, text_value.ptr, text_value.len));
    }

    pub fn close(self: *TextWriter) Error!void {
        try check(c.lk_text_stream_writer_close(self.handle));
    }

    pub fn cancel(self: *TextWriter, reason: [:0]const u8) Error!void {
        try check(c.lk_text_stream_writer_cancel(self.handle, reason.ptr));
    }

    pub fn id(self: *const TextWriter, allocator: std.mem.Allocator) Error![]u8 {
        return readString(allocator, c.lk_text_stream_writer_id, .{self.handle});
    }
};

pub const ByteWriter = struct {
    handle: *c.lk_byte_stream_writer_t,

    pub fn deinit(self: *ByteWriter) void {
        c.lk_byte_stream_writer_destroy(self.handle);
        self.* = undefined;
    }

    pub fn write(self: *ByteWriter, bytes: []const u8) Error!void {
        try check(c.lk_byte_stream_writer_write(self.handle, bytes.ptr, bytes.len));
    }

    pub fn close(self: *ByteWriter) Error!void {
        try check(c.lk_byte_stream_writer_close(self.handle));
    }

    pub fn cancel(self: *ByteWriter, reason: [:0]const u8) Error!void {
        try check(c.lk_byte_stream_writer_cancel(self.handle, reason.ptr));
    }

    pub fn id(self: *const ByteWriter, allocator: std.mem.Allocator) Error![]u8 {
        return readString(allocator, c.lk_byte_stream_writer_id, .{self.handle});
    }
};

pub const RpcResult = struct {
    handle: *c.lk_rpc_result_t,

    pub fn deinit(self: *RpcResult) void {
        c.lk_rpc_result_destroy(self.handle);
        self.* = undefined;
    }

    pub fn ok(self: *const RpcResult) bool {
        return c.lk_rpc_result_ok(self.handle) != 0;
    }

    pub fn payload(self: *const RpcResult, allocator: std.mem.Allocator) Error![]u8 {
        return readString(allocator, c.lk_rpc_result_payload, .{self.handle});
    }

    pub fn errorCode(self: *const RpcResult) u32 {
        return c.lk_rpc_result_error_code(self.handle);
    }
};

/// Keep the source alive until all tracks using it are destroyed.
pub const AudioSource = struct {
    handle: *c.lk_audio_source_t,
    channels: u32,

    pub fn create(options: *const c.lk_audio_source_options_t) Error!AudioSource {
        var source: ?*c.lk_audio_source_t = null;
        try check(c.lk_audio_source_create(options, &source));
        return .{ .handle = source orelse return error.NativeFailure, .channels = options.num_channels };
    }

    pub fn deinit(self: *AudioSource) Error!void {
        try check(c.lk_audio_source_destroy(self.handle));
        self.* = undefined;
    }

    pub fn capture(self: *AudioSource, samples: []const i16) Error!void {
        if (self.channels == 0 or samples.len % self.channels != 0 or samples.len / self.channels > std.math.maxInt(u32)) return error.InvalidArgument;
        try check(c.lk_audio_source_capture_frame(self.handle, samples.ptr, @intCast(samples.len / self.channels)));
    }
};

pub const VideoSource = struct {
    handle: *c.lk_video_source_t,

    pub fn create(options: *const c.lk_video_source_options_t) Error!VideoSource {
        var source: ?*c.lk_video_source_t = null;
        try check(c.lk_video_source_create(options, &source));
        return .{ .handle = source orelse return error.NativeFailure };
    }

    pub fn deinit(self: *VideoSource) Error!void {
        try check(c.lk_video_source_destroy(self.handle));
        self.* = undefined;
    }

    pub fn captureI420(self: *VideoSource, bytes: []const u8, width: u32, height: u32, timestamp_us: i64) Error!void {
        try check(c.lk_video_source_capture_i420(self.handle, bytes.ptr, bytes.len, width, height, timestamp_us));
    }
};

pub const LocalTrack = struct {
    handle: *c.lk_local_track_t,

    pub fn deinit(self: *LocalTrack) Error!void {
        try check(c.lk_local_track_destroy(self.handle));
        self.* = undefined;
    }

    pub fn publish(self: *LocalTrack, room: *Room, options: *const c.lk_track_publish_options_t) Error!void {
        try check(c.lk_local_track_publish(room.handle, self.handle, options));
    }

    pub fn unpublish(self: *LocalTrack, stop_on_unpublish: bool) Error!void {
        try check(c.lk_local_track_unpublish(self.handle, @intFromBool(stop_on_unpublish)));
    }
};

pub const DataTrack = struct {
    handle: *c.lk_local_data_track_t,

    pub fn deinit(self: *DataTrack) Error!void {
        try checkDataTrack(c.lk_local_data_track_destroy(self.handle));
        self.* = undefined;
    }

    pub fn push(self: *DataTrack, bytes: []const u8, timestamp: ?u64) Error!void {
        try checkDataTrack(c.lk_local_data_track_try_push(self.handle, bytes.ptr, bytes.len, @intFromBool(timestamp != null), timestamp orelse 0));
    }

    pub fn unpublish(self: *DataTrack) Error!void {
        try checkDataTrack(c.lk_local_data_track_unpublish(self.handle));
    }
};

/// Use the native initializer for every versioned C ABI options structure.
pub fn roomCallbacks() c.lk_room_callbacks_t {
    var callbacks: c.lk_room_callbacks_t = undefined;
    c.lk_room_callbacks_init(&callbacks);
    return callbacks;
}

pub fn connectOptions() c.lk_room_connect_options_t {
    var options: c.lk_room_connect_options_t = undefined;
    c.lk_room_connect_options_init(&options);
    return options;
}

pub fn tokenSourceFetchOptions() c.lk_token_source_fetch_options_t {
    var options: c.lk_token_source_fetch_options_t = undefined;
    c.lk_token_source_fetch_options_init(&options);
    return options;
}

pub fn e2eeOptions() c.lk_e2ee_options_t {
    var options: c.lk_e2ee_options_t = undefined;
    c.lk_e2ee_options_init(&options);
    return options;
}

pub fn dataPublishOptions() c.lk_data_publish_options_t {
    var options: c.lk_data_publish_options_t = undefined;
    c.lk_data_publish_options_init(&options);
    return options;
}

pub fn streamTextOptions() c.lk_stream_text_options_t {
    var options: c.lk_stream_text_options_t = undefined;
    c.lk_stream_text_options_init(&options);
    return options;
}

pub fn streamBytesOptions() c.lk_stream_bytes_options_t {
    var options: c.lk_stream_bytes_options_t = undefined;
    c.lk_stream_bytes_options_init(&options);
    return options;
}

pub fn rpcOptions() c.lk_rpc_perform_options_t {
    var options: c.lk_rpc_perform_options_t = undefined;
    c.lk_rpc_perform_options_init(&options);
    return options;
}

pub fn audioSourceOptions() c.lk_audio_source_options_t {
    var options: c.lk_audio_source_options_t = undefined;
    c.lk_audio_source_options_init(&options);
    return options;
}

pub fn videoSourceOptions() c.lk_video_source_options_t {
    var options: c.lk_video_source_options_t = undefined;
    c.lk_video_source_options_init(&options);
    return options;
}

pub fn trackPublishOptions() c.lk_track_publish_options_t {
    var options: c.lk_track_publish_options_t = undefined;
    c.lk_track_publish_options_init(&options);
    return options;
}

pub fn dataTrackPublishOptions() c.lk_data_track_publish_options_t {
    var options: c.lk_data_track_publish_options_t = undefined;
    c.lk_data_track_publish_options_init(&options);
    return options;
}

test "C ABI constants and signatures are visible" {
    try std.testing.expect(@sizeOf(c.lk_room_callbacks_t) > @sizeOf(usize));
    try std.testing.expect(c.LK_STATUS_OK == 0);
    _ = c.lk_room_create;
    _ = c.lk_room_send_text;
    _ = c.lk_room_publish_data_track;
    _ = c.lk_room_perform_rpc;
}

test "native runtime and room lifecycle" {
    try init();
    defer shutdown() catch {};

    const sdk_version = try version(std.testing.allocator);
    defer std.testing.allocator.free(sdk_version);
    try std.testing.expect(sdk_version.len > 0);

    var room = try Room.create();
    defer room.deinit();
    try std.testing.expect(!room.isConnected());

    const callbacks = roomCallbacks();
    try room.setCallbacks(&callbacks);

    const room_name = try room.name(std.testing.allocator);
    defer std.testing.allocator.free(room_name);

    const options = connectOptions();
    try std.testing.expect(options.struct_size == @sizeOf(c.lk_room_connect_options_t));
    const publish_options = dataPublishOptions();
    try std.testing.expect(publish_options.struct_size == @sizeOf(c.lk_data_publish_options_t));
}

test "native error is copied before another C call" {
    try std.testing.expectError(error.NativeFailure, check(c.lk_room_create(null)));
    const message = try lastError(std.testing.allocator);
    defer std.testing.allocator.free(message);
    try std.testing.expect(message.len > 0);
}
