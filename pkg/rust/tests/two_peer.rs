use livekit_client_cpp::*;
#[path = "cases/media.rs"]
mod media_checks;
mod support;
use std::{
    env, thread,
    time::{Duration, Instant},
};
use support::block_on;

fn wait_until(mut condition: impl FnMut() -> bool) {
    let deadline = Instant::now() + Duration::from_secs(15);
    while !condition() {
        assert!(Instant::now() < deadline, "two-peer test timed out");
        thread::sleep(Duration::from_millis(10));
    }
}

fn credentials() -> Option<(String, String, String)> {
    if env::var("BUILD_INTEGRATION_TESTS").as_deref() != Ok("ON") {
        eprintln!("SKIP: BUILD_INTEGRATION_TESTS=ON is required");
        return None;
    }
    match (
        env::var("LIVEKIT_URL"),
        env::var("LIVEKIT_TOKEN"),
        env::var("LIVEKIT_TOKEN_B"),
    ) {
        (Ok(url), Ok(a), Ok(b)) => Some((url, a, b)),
        _ => {
            eprintln!("SKIP: two participant test credentials are required");
            None
        }
    }
}
fn sdk() -> Sdk {
    let sdk = Sdk::new().unwrap();
    sdk.set_log_levels(
        ffi::LK_LOG_LEVEL_WARNING,
        ffi::LK_LOG_LEVEL_WARNING,
        ffi::LK_LOG_LEVEL_WARNING,
    )
    .unwrap();
    sdk
}

#[test]
#[ignore = "requires local test server and two tokens"]
fn async_data_rpc_chat_and_events() {
    let Some((url, a_token, b_token)) = credentials() else {
        return;
    };
    let sdk = sdk();
    let a = sdk.async_room().unwrap();
    let mut b = sdk.async_room().unwrap();
    block_on(b.register_rpc_method("rust.echo", |i| RpcResponse::Success(i.payload))).unwrap();
    let connect_a = a.connect(&url, &a_token);
    let connect_b = b.connect(&url, &b_token);
    block_on(connect_a).unwrap();
    block_on(connect_b).unwrap();
    let b_identity = block_on(b.local_participant()).unwrap().identity;
    wait_until(|| !block_on(a.remote_participants()).unwrap().is_empty());
    let handle = a.handle();
    std::thread::spawn(move || {
        block_on(handle.publish_data(&[0, 255, 42], "async-smoke", true)).unwrap()
    })
    .join()
    .unwrap();
    loop {
        if let Event::Data { data, topic, .. } = block_on(b.next_event()).unwrap() {
            if topic == "async-smoke" {
                assert_eq!(data, [0, 255, 42]);
                break;
            }
        }
    }
    assert_eq!(
        block_on(a.perform_rpc(&b_identity, "rust.echo", "async rpc", 5000)).unwrap(),
        RpcResponse::Success("async rpc".into())
    );
    let chat = block_on(a.send_chat_message("async chat")).unwrap();
    loop {
        if let Event::Chat(event) = block_on(b.next_event()).unwrap() {
            if event.id == chat.id {
                assert_eq!(event.message, "async chat");
                break;
            }
        }
    }
    block_on(a.call(move |room| room.edit_chat_message(&chat, "edited chat"))).unwrap();
    loop {
        if let Event::Chat(event) = block_on(b.next_event()).unwrap() {
            if event.message == "edited chat" {
                assert!(event.edit_timestamp.is_some());
                break;
            }
        }
    }
    block_on(a.disconnect()).unwrap();
    block_on(b.disconnect()).unwrap();
}

#[test]
#[ignore = "requires local test server and two tokens"]
fn data_track_schema_timestamped_reader_and_snapshot() {
    let Some((url, a_token, b_token)) = credentials() else {
        return;
    };
    let sdk = sdk();
    let mut a = sdk.room().unwrap();
    let mut b = sdk.room().unwrap();
    a.connect(&url, &a_token).unwrap();
    b.connect(&url, &b_token).unwrap();
    a.wait_connected(10000).unwrap();
    b.wait_connected(10000).unwrap();
    wait_until(|| !b.remote_participants().unwrap().is_empty());
    let identity = a.local_participant().unwrap().identity;
    let id = SchemaId {
        name: "rust.schema".into(),
        encoding: ffi::LK_DATA_TRACK_SCHEMA_ENCODING_JSON_SCHEMA,
        custom_encoding: String::new(),
    };
    let definition = br#"{"type":"object"}"#;
    a.store_data_schema(&id, definition).unwrap();
    let schema = b.data_schema(&identity, &id).unwrap();
    assert_eq!(schema.definition().unwrap(), definition);
    let track = a
        .publish_data_track_with_options(&DataTrackPublishOptions {
            name: "rust-data".into(),
            schema: Some(id),
            frame_encoding: Some(ffi::LK_DATA_TRACK_FRAME_ENCODING_JSON),
            ..DataTrackPublishOptions::default()
        })
        .unwrap();
    let sid = track.sid().unwrap();
    wait_until(|| b.remote_data_tracks().unwrap().iter().any(|t| t.sid == sid));
    let mut reader = b.subscribe_data_track(&identity, &sid, 4).unwrap();
    let options = DataSubscriptionOptions {
        capacity: 4,
        target_fps: Some(30),
        ..DataSubscriptionOptions::default()
    };
    b.update_data_subscription(&identity, &sid, &options)
        .unwrap();
    let deadline = Instant::now() + Duration::from_secs(10);
    let frame = loop {
        track.push(br#"{"n":1}"#, Some(123456)).unwrap();
        if let Read::Frame(frame) = reader.read_for(100).unwrap() {
            break frame;
        }
        assert!(Instant::now() < deadline, "DataTrack reader timed out");
    };
    assert_eq!(frame.bytes().unwrap(), br#"{"n":1}"#);
    assert_eq!(frame.timestamp(), Some(123456));
    let snapshot = b.remote_data_tracks().unwrap();
    track.unpublish().unwrap();
    reader.close();
    assert!(reader.is_closed());
    a.disconnect().unwrap();
    b.disconnect().unwrap();
    drop(a);
    drop(b);
    assert_eq!(snapshot[0].sid, sid);
    assert_eq!(schema.definition().unwrap(), definition);
}

#[test]
#[ignore = "requires local test server and test E2EE key"]
fn e2ee_options_keys_and_token_provider() {
    let Some((url, a_token, _)) = credentials() else {
        return;
    };
    let Ok(key) = env::var("LIVEKIT_E2EE_KEY") else {
        eprintln!("SKIP: LIVEKIT_E2EE_KEY is required");
        return;
    };
    let sdk = sdk();
    let mut room = sdk.room().unwrap();
    let options = AdvancedConnectOptions {
        e2ee: Some(AdvancedE2eeOptions {
            base: E2eeOptions {
                shared_key: key.into_bytes(),
                key_derivation: ffi::LK_E2EE_KEY_DERIVATION_PBKDF2_SHA256,
            },
            ..AdvancedE2eeOptions::default()
        }),
        reconnect_policy: Some(std::sync::Arc::new(|_| None)),
        ..AdvancedConnectOptions::default()
    };
    let fetch_options = TokenFetchOptions {
        room_name: "requested-room".into(),
        participant_identity: "requested-identity".into(),
        participant_attributes: vec![("source".into(), "rust".into())],
        ..TokenFetchOptions::default()
    };
    let expected = fetch_options.clone();
    room.connect_with_token_provider(&options, &fetch_options, move |request| {
        assert_eq!(request.options, expected);
        Ok(TokenResponse {
            server_url: url.clone(),
            participant_token: a_token.clone(),
        })
    })
    .unwrap();
    assert!(room.e2ee_is_configured());
    assert!(room.e2ee_is_enabled());
    let key = vec![13u8; 32];
    room.e2ee_set_shared_key(&key, 1).unwrap();
    assert_eq!(room.e2ee_export_shared_key(1).unwrap(), key);
    room.e2ee_set_data_key_index(1).unwrap();
    assert_eq!(room.e2ee_data_key_index(), 1);
    room.e2ee_ratchet_shared_key(1).unwrap();
    assert_ne!(room.e2ee_export_shared_key(1).unwrap(), key);
    room.e2ee_set_participant_key("remote", &key, 1).unwrap();
    assert_eq!(room.e2ee_export_participant_key("remote", 1).unwrap(), key);
    room.e2ee_remove_participant_key("remote", 1).unwrap();
    room.e2ee_remove_shared_key(1).unwrap();
    room.e2ee_frame_cryptors().unwrap();
    room.disconnect().unwrap();
}

#[test]
#[ignore = "requires explicit integration opt-in and two tokens for the same test room"]
fn data_rpc_streams_and_track_lifetimes() {
    if env::var("BUILD_INTEGRATION_TESTS").as_deref() != Ok("ON") {
        eprintln!("SKIP: set BUILD_INTEGRATION_TESTS=ON to enable Rust integration tests");
        return;
    }
    let (Ok(url), Ok(a_token), Ok(b_token)) = (
        env::var("LIVEKIT_URL"),
        env::var("LIVEKIT_TOKEN"),
        env::var("LIVEKIT_TOKEN_B"),
    ) else {
        eprintln!("SKIP: LIVEKIT_URL, LIVEKIT_TOKEN and LIVEKIT_TOKEN_B are required");
        return;
    };
    let sdk = Sdk::new().unwrap();
    let mut a = sdk.room().unwrap();
    let mut b = sdk.room().unwrap();
    b.register_rpc_method("rust.echo", |i| RpcResponse::Success(i.payload))
        .unwrap();
    b.register_text_stream("rust.smoke").unwrap();
    b.register_byte_stream("rust.smoke").unwrap();
    a.connect(&url, &a_token).unwrap();
    b.connect(&url, &b_token).unwrap();
    wait_until(|| a.is_connected() && b.is_connected());
    wait_until(|| !a.remote_participants().unwrap().is_empty());
    let b_identity = b.local_participant().unwrap().identity;
    a.publish_data(&[1, 0, 255], "rust.smoke", true).unwrap();
    wait_until(|| {
        while let Ok(event) = b.try_recv() {
            if matches!(event,Event::Data {data,topic,..} if data == [1,0,255] && topic == "rust.smoke")
            {
                return true;
            }
        }
        false
    });
    assert_eq!(
        a.perform_rpc(&b_identity, "rust.echo", "RPC smoke", 5000)
            .unwrap(),
        RpcResponse::Success("RPC smoke".into())
    );
    let options = DataStreamOptions {
        topic: "rust.smoke".into(),
        attributes: vec![("language".into(), "中文".into())],
        attached_stream_ids: vec!["attachment".into()],
        reply_to_stream_id: "previous".into(),
        total_size: Some(12),
        compress: true,
        ..DataStreamOptions::default()
    };
    let mut writer = a.stream_text_with_options(&options).unwrap();
    let stream_id = writer.id().unwrap();
    let stream_info = writer.info().unwrap();
    assert_eq!(stream_info.attributes, options.attributes);
    assert_eq!(stream_info.attached_stream_ids, options.attached_stream_ids);
    assert_eq!(stream_info.reply_to_stream_id, "previous");
    writer.write("first ").unwrap();
    writer.write("世界").unwrap();
    writer.close().unwrap();
    wait_until(|| {
        while let Ok(event) = a.try_recv() {
            if let Event::StreamCompleted(event) = event {
                if event.stream_id == stream_id {
                    assert_eq!(event.status, ffi::LK_DATA_STREAM_COMPLETION_COMPLETED);
                    assert_eq!(event.bytes_sent, 12);
                    return true;
                }
            }
        }
        false
    });
    let mut content = Vec::new();
    wait_until(|| {
        while let Ok(event) = b.try_recv() {
            if let Event::TextStream(event) = event {
                content.extend(event.content);
                if event.kind == ffi::LK_DATA_STREAM_EVENT_CLOSED {
                    return true;
                }
            }
        }
        false
    });
    assert_eq!(String::from_utf8(content).unwrap(), "first 世界");
    let mut bytes = a
        .stream_bytes("rust.smoke", "smoke.bin", "application/octet-stream")
        .unwrap();
    bytes.write(&[1, 0, 255]).unwrap();
    bytes.close().unwrap();
    let mut content = Vec::new();
    wait_until(|| {
        while let Ok(event) = b.try_recv() {
            if let Event::ByteStream(event) = event {
                content.extend(event.content);
                if event.kind == ffi::LK_DATA_STREAM_EVENT_CLOSED {
                    return true;
                }
            }
        }
        false
    });
    assert_eq!(content, [1, 0, 255]);
    let audio = sdk.audio_source(&AudioOptions::default()).unwrap();
    let track = a.create_audio_track("rust-owned-audio", &audio).unwrap();
    a.publish_track(&track, ffi::LK_TRACK_SOURCE_MICROPHONE)
        .unwrap();
    assert_eq!(
        b.publish_track(&track, ffi::LK_TRACK_SOURCE_MICROPHONE)
            .unwrap_err()
            .status,
        ffi::LK_STATUS_INVALID_ARGUMENT
    );
    drop(track);
    drop(audio);
    let video = sdk.video_source(false).unwrap();
    video
        .capture_i420(&vec![128; 64 * 48 * 3 / 2], 64, 48, 0)
        .unwrap();
    let track = a.create_video_track("rust-owned-video", &video).unwrap();
    a.publish_track(&track, ffi::LK_TRACK_SOURCE_CAMERA)
        .unwrap();
    let stats = track.rtc_stats_snapshot().unwrap();
    drop(video);
    drop(sdk);
    eprintln!("two-peer data/RPC/streams/tracks passed; disconnecting publisher");
    a.disconnect().unwrap();
    drop(a);
    track
        .video_source()
        .unwrap()
        .capture_i420(&vec![128; 16 * 16 * 3 / 2], 16, 16, 0)
        .unwrap();
    assert!(track.unpublish(false).is_err());
    stats.entries().unwrap();
    b.disconnect().unwrap();
    eprintln!("two-peer rooms disconnected");
}
