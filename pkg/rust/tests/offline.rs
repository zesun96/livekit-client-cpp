use livekit_client_cpp::{
    ffi, AudioOptions, ConnectOptions, RpcResponse, SchemaId, Sdk, VideoInput, VideoMetadata,
};

#[test]
fn offline_lifecycle_snapshots_and_failures() {
    let sdk = Sdk::new().unwrap();
    let second = Sdk::new().unwrap();
    assert!(!sdk.version().unwrap().is_empty());
    assert!(sdk.room_with_event_capacity(0).is_err());
    let mut room = sdk.room().unwrap();
    drop(sdk);
    drop(second);
    // The room still owns the initialized SDK.
    assert!(!room.is_connected());
    assert_eq!(room.state(), ffi::LK_ROOM_STATE_DISCONNECTED);
    assert!(room.remote_participants().unwrap().is_empty());
    let local = room.local_participant().unwrap();
    let failure = room
        .publish_data(&[0, 255, 0], "offline", true)
        .unwrap_err();
    assert_ne!(failure.status, ffi::LK_STATUS_OK);
    assert!(!failure.message.is_empty());
    let saved = failure.message.clone();
    assert!(room.send_text("offline", "test").is_err());
    assert!(room.send_bytes(&[], "test").is_err());
    assert_eq!(failure.message, saved);
    assert_eq!(
        room.connect("invalid\0url", "token").unwrap_err().status,
        ffi::LK_STATUS_INVALID_ARGUMENT
    );
    drop(room);
    // Participant values are copied out of the destroyed snapshot and room.
    assert!(local.identity.is_empty());
    let restarted = Sdk::new().unwrap();
    assert!(!restarted.version().unwrap().is_empty());
    drop(restarted.room().unwrap());
}

#[test]
fn external_media_ownership_and_invalid_frames() {
    let sdk = Sdk::new().unwrap();
    let options = AudioOptions {
        channels: 2,
        ..AudioOptions::default()
    };
    let audio = sdk.audio_source(&options).unwrap();
    assert!(audio.capture(&[1, 2, 3]).is_err());
    assert!(audio.capture(&[]).is_err());
    audio
        .capture(&vec![
            0;
            (options.sample_rate / 100 * options.channels) as usize
        ])
        .unwrap();
    audio.clear_queue().unwrap();
    assert_eq!(audio.queued_duration_ms().unwrap(), 0);
    audio.wait_for_playout(0).unwrap();
    let video = sdk.video_source(false).unwrap();
    let data = vec![128; 16 * 16 * 3 / 2];
    video.capture_i420(&data, 16, 16, 0).unwrap();
    assert!(video.capture_i420(&[], 16, 16, 0).is_err());
    let metadata = VideoMetadata {
        user_timestamp_us: Some(123),
        frame_id: Some(7),
        user_data: Some(vec![0, 255]),
    };
    video
        .capture_frame(&VideoInput {
            data: &data,
            width: 16,
            height: 16,
            timestamp_us: 1,
            rotation: ffi::LK_VIDEO_ROTATION_0,
            format: ffi::LK_VIDEO_BUFFER_I420,
            planes: &[],
            metadata: Some(&metadata),
        })
        .unwrap();
    let mut room = sdk.room().unwrap();
    // Native track creation requires a connected local participant.
    assert!(room.create_audio_track("audio", &audio).is_err());
    assert!(room.create_video_track("video", &video).is_err());
    let retained_audio = audio.clone();
    let retained_video = video.clone();
    drop(audio);
    drop(video);
    drop(sdk);
    // Sources retain SDK initialization, including after room destruction.
    drop(room);
    retained_video.capture_i420(&data, 16, 16, 2).unwrap();
    retained_audio.clear_queue().unwrap();
}

#[test]
fn offline_advanced_operations_and_callback_context_lifetime() {
    let sdk = Sdk::new().unwrap();
    let mut room = sdk.room().unwrap();
    assert!(room.remote_tracks().unwrap().is_empty());
    assert!(room.stream_text("offline").is_err());
    assert!(room
        .stream_bytes("offline", "test.bin", "application/octet-stream")
        .is_err());
    assert!(room.audio_stream("missing", "missing", 2).is_err());
    assert!(room.video_stream("missing", "missing", 2).is_err());
    assert!(room.audio_stream("missing", "missing", 0).is_err());
    assert!(room
        .start_recording("missing", "missing", "unused.wav", 2)
        .is_err());
    assert!(room.publish_data_track("offline", None).is_err());
    assert!(room.subscribe_data_track("missing", "missing", 2).is_err());
    let schema = SchemaId {
        name: "offline".into(),
        encoding: ffi::LK_DATA_TRACK_SCHEMA_ENCODING_JSON_SCHEMA,
        custom_encoding: String::new(),
    };
    assert!(room
        .store_data_schema(&schema, br#"{"type":"object"}"#)
        .is_err());
    assert!(!room.e2ee_is_configured());
    assert!(room.e2ee_set_shared_key(&[1; 32], 0).is_err());
    assert!(room.e2ee_export_shared_key(0).is_err());
    assert!(room.e2ee_export_participant_key("missing", 0).is_err());
    room.register_rpc_method("echo", |i| RpcResponse::Success(i.payload))
        .unwrap();
    assert!(room
        .register_rpc_method("echo", |_| RpcResponse::Success(String::new()))
        .is_err());
    room.unregister_rpc_method("echo").unwrap();
    room.register_rpc_method("echo", |i| RpcResponse::Success(i.payload))
        .unwrap();
    assert!(matches!(
        room.perform_rpc("missing", "echo", "payload", 100).unwrap(),
        RpcResponse::Failure { .. }
    ));
    room.register_text_stream("test").unwrap();
    room.register_byte_stream("test").unwrap();
    room.unregister_text_stream("test").unwrap();
    room.unregister_byte_stream("test").unwrap();
    // A failed provider invokes no network connection and remains owned until
    // the room is destroyed, so future recovery cannot borrow freed context.
    let marker = std::sync::Arc::new(());
    let weak = std::sync::Arc::downgrade(&marker);
    assert!(room
        .connect_with_token_source(&ConnectOptions::default(), move |_| {
            let _keep_alive = &marker;
            Err(livekit_client_cpp::Error {
                status: ffi::LK_STATUS_OPERATION_FAILED,
                message: "test provider failure".into(),
            })
        })
        .is_err());
    assert!(weak.upgrade().is_some());
    drop(room);
    assert!(weak.upgrade().is_none());
}

#[test]
fn diagnostic_guards_release_global_sinks() {
    let sdk = Sdk::new().unwrap();
    let logs = sdk.log_events(4).unwrap();
    assert!(sdk.log_events(4).is_err());
    let traces = sdk
        .trace_events(4, ffi::LK_TRACE_CATEGORY_ALL as u64)
        .unwrap();
    assert!(sdk
        .trace_events(4, ffi::LK_TRACE_CATEGORY_ALL as u64)
        .is_err());
    let room = sdk.room().unwrap();
    drop(room);
    // Room construction/destruction emits lifecycle records.
    assert!(traces.try_recv().is_ok());
    drop(logs);
    drop(traces);
    drop(sdk.log_events(1).unwrap());
    drop(
        sdk.trace_events(1, ffi::LK_TRACE_CATEGORY_ALL as u64)
            .unwrap(),
    );
}
