use livekit_client_cpp::*;
use std::{
    env, thread,
    time::{Duration, Instant},
};

fn wait_until(mut condition: impl FnMut() -> bool) {
    let deadline = Instant::now() + Duration::from_secs(15);
    while !condition() {
        assert!(Instant::now() < deadline, "two-peer test timed out");
        thread::sleep(Duration::from_millis(10));
    }
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
    let mut writer = a.stream_text("rust.smoke").unwrap();
    writer.write("first ").unwrap();
    writer.write("世界").unwrap();
    writer.close().unwrap();
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
    let track = a.create_video_track("rust-owned-video", &video).unwrap();
    a.publish_track(&track, ffi::LK_TRACK_SOURCE_CAMERA)
        .unwrap();
    let stats = track.rtc_stats_snapshot().unwrap();
    drop(video);
    drop(sdk);
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
}
