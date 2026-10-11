const std = @import("std");
const livekit = @import("livekit");
const c = livekit.c;

test "room lifecycle, detached snapshots, and disconnected operations" {
    try livekit.init();
    defer livekit.shutdown() catch unreachable;

    var room = try livekit.Room.create();
    defer room.deinit();
    try std.testing.expect(!room.isConnected());
    try std.testing.expectEqual(@as(c.lk_room_state_t, c.LK_ROOM_STATE_DISCONNECTED), room.state());

    var callbacks = livekit.roomCallbacks();
    try std.testing.expectEqual(@sizeOf(c.lk_room_callbacks_t), callbacks.struct_size);
    try room.setCallbacks(&callbacks);

    var remote: ?*c.lk_remote_participant_list_t = null;
    try livekit.check(c.lk_room_create_remote_participant_snapshot(room.handle, &remote));
    defer c.lk_remote_participant_list_destroy(remote);
    try std.testing.expectEqual(@as(usize, 0), c.lk_remote_participant_list_count(remote));

    try std.testing.expectError(error.NativeFailure, room.publishData("offline", null));
    const failure = try livekit.lastError(std.testing.allocator);
    defer std.testing.allocator.free(failure);
    try std.testing.expect(failure.len > 0);
    try std.testing.expectError(error.NativeFailure, room.sendText("offline", null));
    try std.testing.expectError(error.NativeFailure, room.sendBytes("offline", null));
}

test "native option initializers and binary safe string reader" {
    try livekit.init();
    defer livekit.shutdown() catch unreachable;

    const connection = livekit.connectOptions();
    try std.testing.expectEqual(@sizeOf(c.lk_room_connect_options_t), connection.struct_size);
    const data = livekit.dataPublishOptions();
    try std.testing.expectEqual(@sizeOf(c.lk_data_publish_options_t), data.struct_size);
    try std.testing.expect(data.reliable != 0);

    var text_options: c.lk_text_send_options_t = undefined;
    c.lk_text_send_options_init(&text_options);
    try std.testing.expectEqual(@sizeOf(c.lk_text_send_options_t), text_options.struct_size);
    var byte_options: c.lk_byte_send_options_t = undefined;
    c.lk_byte_send_options_init(&byte_options);
    try std.testing.expectEqual(@sizeOf(c.lk_byte_send_options_t), byte_options.struct_size);

    const sdk_version = try livekit.version(std.testing.allocator);
    defer std.testing.allocator.free(sdk_version);
    try std.testing.expect(sdk_version.len > 0);
}

test "media device enumeration does not open devices" {
    try livekit.init();
    defer livekit.shutdown() catch unreachable;

    var devices: ?*c.lk_media_device_list_t = null;
    try livekit.check(c.lk_media_device_list_create(&devices));
    defer c.lk_media_device_list_destroy(devices);

    if (c.lk_media_device_list_count(devices) > 0) {
        var info: c.lk_media_device_info_t = undefined;
        try livekit.check(c.lk_media_device_list_info(devices, 0, &info));
        const id = try livekit.readString(std.testing.allocator, c.lk_media_device_list_id, .{ devices, @as(usize, 0) });
        defer std.testing.allocator.free(id);
        try std.testing.expect(id.len > 0);
    }
}

test "owned snapshot and external media source wrappers" {
    try livekit.init();
    defer livekit.shutdown() catch unreachable;
    var room = try livekit.Room.create();
    defer room.deinit();

    var local = try room.localSnapshot();
    defer local.deinit();
    const identity = try local.identity(std.testing.allocator);
    defer std.testing.allocator.free(identity);

    var remote = try room.remoteParticipants();
    defer remote.deinit();
    try std.testing.expectEqual(@as(usize, 0), remote.count());
    try std.testing.expectError(error.NativeFailure, remote.at(0));

    var audio_options = livekit.audioSourceOptions();
    audio_options.sample_rate = 48000;
    audio_options.num_channels = 2;
    audio_options.queue_size_ms = 200;
    var audio = try livekit.AudioSource.create(&audio_options);
    defer audio.deinit() catch unreachable;
    try std.testing.expectError(error.InvalidArgument, audio.capture(&.{ 1, 2, 3 }));

    var video_options = livekit.videoSourceOptions();
    var video = try livekit.VideoSource.create(&video_options);
    defer video.deinit() catch unreachable;

    try std.testing.expectError(error.NativeFailure, room.createAudioTrack("zig-test-audio", &audio));
    try std.testing.expectError(error.NativeFailure, room.createVideoTrack("zig-test-video", &video));

    const publish_options = livekit.trackPublishOptions();
    try std.testing.expectEqual(@sizeOf(c.lk_track_publish_options_t), publish_options.struct_size);
    const fetch_options = livekit.tokenSourceFetchOptions();
    try std.testing.expectEqual(@sizeOf(c.lk_token_source_fetch_options_t), fetch_options.struct_size);
    const encryption = livekit.e2eeOptions();
    try std.testing.expectEqual(@sizeOf(c.lk_e2ee_options_t), encryption.struct_size);
    try std.testing.expect(!room.e2eeConfigured());
    try std.testing.expect(!room.e2eeEnabled());
}

test "advanced operation errors are available without a server" {
    try livekit.init();
    defer livekit.shutdown() catch unreachable;
    var room = try livekit.Room.create();
    defer room.deinit();

    const text_options = livekit.streamTextOptions();
    try std.testing.expectError(error.NativeFailure, room.openTextStream(&text_options));
    const byte_options = livekit.streamBytesOptions();
    try std.testing.expectError(error.NativeFailure, room.openByteStream(&byte_options));

    var rpc_options = livekit.rpcOptions();
    rpc_options.destination_identity = "missing";
    rpc_options.method = "zig.test";
    rpc_options.payload = "hello";
    var rpc_result = try room.performRpc(&rpc_options);
    defer rpc_result.deinit();
    try std.testing.expect(!rpc_result.ok());

    var track_options = livekit.dataTrackPublishOptions();
    track_options.name = "zig-test";
    try std.testing.expectError(error.NativeFailure, room.publishDataTrack(&track_options));
}
