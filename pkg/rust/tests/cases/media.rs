use super::{credentials, sdk, wait_until};
use livekit_client_cpp::*;
use std::{env, thread, time::Duration};

#[test]
#[ignore = "requires local test server; remote audio playback may open a physical device"]
fn media_frames_metadata_recording_and_reader_lifetime() {
    let Some((url, a_token, b_token)) = credentials() else {
        return;
    };
    let sdk = sdk();
    let mut a = sdk.room().unwrap();
    let mut b = sdk.room().unwrap();
    let _ = b.set_speaker_muted(true);
    a.connect(&url, &a_token).unwrap();
    b.connect(&url, &b_token).unwrap();
    let identity = a.local_participant().unwrap().identity;
    let audio = sdk.audio_source(&AudioOptions::default()).unwrap();
    let at = a.create_audio_track("rust-frames-audio", &audio).unwrap();
    a.publish_track_with_options(
        &at,
        &TrackPublishOptions {
            source: ffi::LK_TRACK_SOURCE_MICROPHONE,
            dtx: false,
            red: false,
            ..TrackPublishOptions::default()
        },
    )
    .unwrap();
    let video = sdk.video_source(false).unwrap();
    let pixels = vec![128u8; 64 * 48 * 3 / 2];
    video.capture_i420(&pixels, 64, 48, 0).unwrap();
    let vt = a.create_video_track("rust-frames-video", &video).unwrap();
    a.publish_track_with_options(
        &vt,
        &TrackPublishOptions {
            source: ffi::LK_TRACK_SOURCE_CAMERA,
            video_codec: ffi::LK_VIDEO_CODEC_VP8,
            simulcast: false,
            user_timestamp: true,
            frame_id: true,
            user_data: true,
            ..TrackPublishOptions::default()
        },
    )
    .unwrap();
    let mut audio_sid = String::new();
    let mut video_sid = String::new();
    wait_until(|| {
        for t in b.remote_tracks().unwrap() {
            if t.name == "rust-frames-audio" {
                audio_sid = t.sid;
            } else if t.name == "rust-frames-video" {
                video_sid = t.sid;
            }
        }
        !audio_sid.is_empty() && !video_sid.is_empty()
    });
    let mut ar = None;
    wait_until(|| {
        if let Ok(s) = b.audio_stream(&identity, &audio_sid, 4) {
            ar = Some(s);
            true
        } else {
            false
        }
    });
    let mut ar = ar.unwrap();
    let mut vr = None;
    wait_until(|| {
        if let Ok(s) = b.video_stream(&identity, &video_sid, 4) {
            vr = Some(s);
            true
        } else {
            false
        }
    });
    let mut vr = vr.unwrap();
    let directory = env::var_os("LIVEKIT_RUST_TEST_OUTPUT")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|| env::temp_dir().join(format!("rust-livekit-{}", std::process::id())));
    std::fs::create_dir_all(&directory).unwrap();
    let path = directory.join("rust-audio.wav");
    let mut recorder = b
        .start_recording(&identity, &audio_sid, path.to_str().unwrap(), 64)
        .unwrap();
    let mut audio_frame = None;
    let mut video_frame = None;
    let mut metadata_seen = false;
    for n in 1..301u32 {
        audio.capture(&[0i16; 480]).unwrap();
        if n % 3 == 0 {
            let m = VideoMetadata {
                user_timestamp_us: Some(u64::from(n) * 10000),
                frame_id: Some(n),
                user_data: Some(vec![0, 255, 7]),
            };
            video
                .capture_frame(&VideoInput {
                    data: &pixels,
                    width: 64,
                    height: 48,
                    timestamp_us: i64::from(n) * 10000,
                    rotation: ffi::LK_VIDEO_ROTATION_0,
                    format: ffi::LK_VIDEO_BUFFER_I420,
                    planes: &[],
                    metadata: Some(&m),
                })
                .unwrap();
        }
        if let Read::Frame(frame) = ar.read_for(0).unwrap() {
            assert_eq!(frame.channels().unwrap(), 1);
            assert!(!frame.samples().unwrap().is_empty());
            audio_frame = Some(frame);
        }
        if let Read::Frame(frame) = vr.read_for(0).unwrap() {
            assert_eq!(frame.dimensions().unwrap(), (64, 48));
            assert!(!frame.bytes().unwrap().is_empty());
            let m = frame.metadata().unwrap();
            if m.frame_id.is_some() {
                assert_eq!(m.user_data, Some(vec![0, 255, 7]));
                metadata_seen = true;
            }
            video_frame = Some(frame);
        }
        thread::sleep(Duration::from_millis(10));
    }
    recorder.stop();
    let stats = recorder.stats().unwrap();
    assert!(stats.frames_written > 0, "recorder received no frames");
    assert!(stats.bytes_written > 0);
    assert!(recorder.error().unwrap().is_empty());
    assert!(audio_frame.is_some(), "no decoded audio received");
    assert!(video_frame.is_some(), "no decoded video received");
    assert!(metadata_seen, "video metadata was not delivered");
    assert_eq!(&std::fs::read(path).unwrap()[..4], b"RIFF");
    let snapshot = b.remote_participant_snapshots().unwrap();
    b.remote_rtc_stats(&identity, &video_sid).unwrap();
    at.unpublish(false).unwrap();
    vt.unpublish(false).unwrap();
    ar.close();
    vr.close();
    a.disconnect().unwrap();
    b.disconnect().unwrap();
    drop(a);
    drop(b);
    drop(sdk);
    assert!(!audio_frame.unwrap().samples().unwrap().is_empty());
    assert!(!video_frame.unwrap().bytes().unwrap().is_empty());
    assert!(!snapshot.is_empty());
    assert!(ar.is_closed());
    assert!(vr.is_closed());
}

#[test]
#[ignore = "requires explicit physical-device opt-in outside the sandbox"]
fn hardware_capture_devices() {
    if env::var("LIVEKIT_HARDWARE_TESTS").as_deref() != Ok("ON") {
        eprintln!("SKIP: LIVEKIT_HARDWARE_TESTS=ON is required");
        return;
    }
    let sdk = sdk();
    let devices = sdk.media_devices().unwrap();
    let mic = devices
        .iter()
        .find(|d| {
            d.kind == ffi::LK_MEDIA_DEVICE_KIND_AUDIO_INPUT && d.label.contains("USB Microphone")
        })
        .expect("independent USB microphone required");
    let options = AudioOptions {
        echo_cancellation: true,
        noise_suppression: true,
        auto_gain_control: true,
        ..AudioOptions::default()
    };
    let source = sdk.microphone(&mic.id, &options).unwrap();
    source.start_microphone().unwrap();
    assert!(source.microphone_is_capturing());
    thread::sleep(Duration::from_millis(400));
    let stats = source.microphone_processing_stats().unwrap();
    assert!(stats.capture_frames_processed > 0);
    source.set_microphone_muted(true).unwrap();
    assert!(source.microphone_is_muted());
    source.set_microphone_muted(false).unwrap();
    source.set_microphone_volume(0.8).unwrap();
    source.set_microphone_processing_options(&options).unwrap();
    assert!(
        source
            .microphone_processing_options()
            .unwrap()
            .noise_suppression
    );
    source.stop_microphone().unwrap();
    assert!(!source.microphone_is_capturing());
    drop(source);
    let output = devices
        .iter()
        .find(|d| {
            d.kind == ffi::LK_MEDIA_DEVICE_KIND_AUDIO_OUTPUT && d.label.contains("USBAudio2.0")
        })
        .expect("USB output required");
    let source = sdk.system_audio(&output.id).unwrap();
    source.start_system_audio().unwrap();
    assert!(source.system_audio_is_capturing());
    thread::sleep(Duration::from_millis(200));
    source.stop_system_audio().unwrap();
    drop(source);
    let camera = devices
        .iter()
        .find(|d| d.kind == ffi::LK_MEDIA_DEVICE_KIND_VIDEO_INPUT)
        .expect("camera required");
    let source = sdk.camera(&camera.id, 640, 480, 30).unwrap();
    source.start_camera().unwrap();
    wait_until(|| source.dimensions().unwrap().0 > 0);
    source.stop_camera().unwrap();
    drop(source);
    let screens = sdk.screen_sources().unwrap();
    let source = sdk.screen_capture(&screens[0].id, 15, true).unwrap();
    source.start_screen().unwrap();
    wait_until(|| source.dimensions().unwrap().0 > 0);
    source.stop_screen().unwrap();
}
