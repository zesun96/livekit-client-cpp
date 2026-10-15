use livekit_client_cpp::*;
use std::{
    env, thread,
    time::{Duration, Instant},
};
type ExampleResult = std::result::Result<(), Box<dyn std::error::Error>>;

fn poll(room: &Room, seconds: u64) {
    let end = Instant::now() + Duration::from_secs(seconds);
    while Instant::now() < end {
        while let Ok(event) = room.try_recv() {
            match event {
                Event::Audio { samples, .. } => println!("audio samples: {}", samples.len()),
                Event::Video { width, height, .. } => println!("video: {width}x{height}"),
                other => println!("{other:?}"),
            }
        }
        thread::sleep(Duration::from_millis(10));
    }
}
fn wait(room: &Room) -> ExampleResult {
    let deadline = Instant::now() + Duration::from_secs(15);
    while !room.is_connected() {
        if Instant::now() >= deadline {
            return Err("connection timed out".into());
        }
        thread::sleep(Duration::from_millis(10));
    }
    Ok(())
}
fn main() -> ExampleResult {
    let scenario = env::args()
        .nth(1)
        .ok_or("specify a scenario; see examples/README.md")?;
    let sdk = Sdk::new()?;
    if scenario == "diagnostics" {
        let logs = sdk.log_events(128)?;
        let traces = sdk.trace_events(128, ffi::LK_TRACE_CATEGORY_ALL as u64)?;
        drop(sdk.room()?);
        while let Ok(record) = logs.try_recv() {
            println!("{record:?}");
        }
        while let Ok(record) = traces.try_recv() {
            println!("{record:?}");
        }
        return Ok(());
    }
    let mut options = ConnectOptions::default();
    if scenario == "e2ee" {
        options.e2ee = Some(E2eeOptions {
            shared_key: env::var("LIVEKIT_E2EE_KEY")?.into_bytes(),
            key_derivation: ffi::LK_E2EE_KEY_DERIVATION_PBKDF2_SHA256,
        });
    }
    if let Ok(url) = env::var("LIVEKIT_ICE_URL") {
        options.ice_servers.push(IceServer {
            urls: vec![url],
            username: env::var("LIVEKIT_ICE_USERNAME").unwrap_or_default(),
            password: env::var("LIVEKIT_ICE_PASSWORD").unwrap_or_default(),
        });
    }
    let mut room = sdk.room()?;
    if scenario == "rpc_receiver" {
        room.register_rpc_method("rust.echo", |i| RpcResponse::Success(i.payload))?;
    }
    if scenario == "token_source" {
        room.connect_with_token_source(&options, |_| {
            let get = |name| {
                env::var(name).map_err(|_| Error {
                    status: ffi::LK_STATUS_INVALID_ARGUMENT,
                    message: format!("missing {name}"),
                })
            };
            Ok(TokenResponse {
                server_url: get("LIVEKIT_URL")?,
                participant_token: get("LIVEKIT_TOKEN")?,
            })
        })?;
    } else {
        room.connect_with_options(
            &env::var("LIVEKIT_URL")?,
            &env::var("LIVEKIT_TOKEN")?,
            &options,
        )?;
    }
    wait(&room)?;
    match scenario.as_str() {
        "streams" => {
            room.register_text_stream("rust.stream")?;
            room.register_byte_stream("rust.stream")?;
            let mut text = room.stream_text("rust.stream")?;
            text.write("Hello ")?;
            text.write("Rust 世界")?;
            text.close()?;
            let mut bytes =
                room.stream_bytes("rust.stream", "example.bin", "application/octet-stream")?;
            bytes.write(&[1, 0, 255])?;
            bytes.write(&[2, 3])?;
            bytes.close()?;
            poll(&room, 5);
        }
        "rpc_receiver" => {
            println!("RPC identity: {}", room.local_participant()?.identity);
            poll(&room, 30);
        }
        "rpc_call" => {
            println!(
                "{:?}",
                room.perform_rpc(
                    &env::var("LIVEKIT_REMOTE_IDENTITY")?,
                    "rust.echo",
                    "Hello from Rust",
                    5000
                )?
            );
        }
        "data_track" => {
            let id = SchemaId {
                name: "rust.example".into(),
                encoding: ffi::LK_DATA_TRACK_SCHEMA_ENCODING_JSON_SCHEMA,
                custom_encoding: String::new(),
            };
            room.store_data_schema(
                &id,
                br#"{"type":"object","properties":{"value":{"type":"integer"}}}"#,
            )?;
            let track = room.publish_data_track("rust-data", Some(&id))?;
            println!("DataTrack SID: {}", track.sid()?);
            for n in 0..50 {
                track.push(format!("{{\"value\":{n}}}").as_bytes(), Some(n))?;
                thread::sleep(Duration::from_millis(100));
            }
            track.unpublish()?;
        }
        "publish_media" => {
            let options = AudioOptions::default();
            let audio = sdk.audio_source(&options)?;
            let video = sdk.video_source(false)?;
            video.capture_i420(&vec![128; 64 * 48 * 3 / 2], 64, 48, 0)?;
            let at = room.create_audio_track("rust-audio", &audio)?;
            let vt = room.create_video_track("rust-video", &video)?;
            room.publish_track(&at, ffi::LK_TRACK_SOURCE_MICROPHONE)?;
            room.publish_track(&vt, ffi::LK_TRACK_SOURCE_CAMERA)?;
            let samples_per_frame = options.sample_rate / 100;
            for n in 0..300u32 {
                let samples: Vec<i16> = (0..samples_per_frame)
                    .flat_map(|s| {
                        let phase =
                            ((n * samples_per_frame + s) as f32) * 440.0 * std::f32::consts::TAU
                                / options.sample_rate as f32;
                        std::iter::repeat((phase.sin() * 2000.0) as i16)
                            .take(options.channels as usize)
                    })
                    .collect();
                audio.capture(&samples)?;
                if n % 3 == 0 {
                    video.capture_i420(
                        &vec![128; 64 * 48 * 3 / 2],
                        64,
                        48,
                        i64::from(n) * 10000,
                    )?;
                }
                thread::sleep(Duration::from_millis(10));
            }
            audio.clear_queue()?;
        }
        "remote_audio" | "remote_video" => {
            let p = env::var("LIVEKIT_REMOTE_IDENTITY")?;
            let t = env::var("LIVEKIT_REMOTE_TRACK_SID")?;
            if scenario == "remote_audio" {
                let mut stream = room.audio_stream(&p, &t, 16)?;
                for _ in 0..30 {
                    if let Read::Frame(frame) = stream.read_for(100)? {
                        println!("{} audio samples", frame.samples()?.len());
                    }
                }
                stream.close();
            } else {
                let mut stream = room.video_stream(&p, &t, 16)?;
                for _ in 0..30 {
                    if let Read::Frame(frame) = stream.read_for(100)? {
                        println!(
                            "video {:?}, metadata {:?}",
                            frame.dimensions()?,
                            frame.metadata()?
                        );
                    }
                }
                stream.close();
            }
        }
        "record" => {
            let path = env::var("LIVEKIT_RECORDING_PATH")
                .unwrap_or_else(|_| "target/livekit-record.wav".into());
            let mut recorder = room.start_recording(
                &env::var("LIVEKIT_REMOTE_IDENTITY")?,
                &env::var("LIVEKIT_REMOTE_TRACK_SID")?,
                &path,
                64,
            )?;
            thread::sleep(Duration::from_secs(5));
            recorder.stop();
            println!("{:?}: {}", recorder.stats()?, recorder.output_path()?);
        }
        "microphone" | "system_audio" => {
            let id = env::var("LIVEKIT_DEVICE_ID").unwrap_or_default();
            let source = if scenario == "microphone" {
                sdk.microphone(&id, &AudioOptions::default())?
            } else {
                sdk.system_audio(&id)?
            };
            let track = room.create_audio_track("rust-capture", &source)?;
            room.publish_track(
                &track,
                if scenario == "microphone" {
                    ffi::LK_TRACK_SOURCE_MICROPHONE
                } else {
                    ffi::LK_TRACK_SOURCE_SCREEN_SHARE_AUDIO
                },
            )?;
            if scenario == "microphone" {
                source.start_microphone()?;
            } else {
                source.start_system_audio()?;
            }
            poll(&room, 5);
            if scenario == "microphone" {
                source.stop_microphone()?;
            } else {
                source.stop_system_audio()?;
            }
        }
        "camera" | "screen" => {
            let id = env::var("LIVEKIT_DEVICE_ID").unwrap_or_default();
            let source = if scenario == "camera" {
                sdk.camera(&id, 640, 480, 30)?
            } else {
                sdk.screen_capture(&id, 15, true)?
            };
            let track = room.create_video_track("rust-capture", &source)?;
            room.publish_track(
                &track,
                if scenario == "camera" {
                    ffi::LK_TRACK_SOURCE_CAMERA
                } else {
                    ffi::LK_TRACK_SOURCE_SCREEN_SHARE
                },
            )?;
            if scenario == "camera" {
                source.start_camera()?;
            } else {
                source.start_screen()?;
            }
            poll(&room, 5);
            if scenario == "camera" {
                source.stop_camera()?;
            } else {
                source.stop_screen()?;
            }
        }
        "e2ee" => {
            println!(
                "E2EE configured: {}, enabled: {}",
                room.e2ee_is_configured(),
                room.e2ee_is_enabled()
            );
            poll(&room, 5);
        }
        "token_source" => poll(&room, 5),
        _ => return Err("unknown scenario; see examples/README.md".into()),
    }
    room.disconnect()?;
    Ok(())
}
