use livekit_client_cpp::Sdk;
use std::{
    env, thread,
    time::{Duration, Instant},
};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let url = env::var("LIVEKIT_URL")?;
    let token = env::var("LIVEKIT_TOKEN")?;
    let sdk = Sdk::new()?;
    let mut room = sdk.room()?;
    room.connect(&url, &token)?;
    let deadline = Instant::now() + Duration::from_secs(15);
    while !room.is_connected() {
        if Instant::now() >= deadline {
            return Err("connection timed out".into());
        }
        thread::sleep(Duration::from_millis(20));
    }
    println!(
        "Connected to {} as {}",
        room.name()?,
        room.local_participant()?.identity
    );
    room.publish_data(b"hello from Rust\0", "rust-example", true)?;
    room.send_text("Hello from Rust", "rust-example")?;
    room.send_bytes(b"Rust byte stream", "rust-example")?;
    let deadline = Instant::now() + Duration::from_secs(10);
    while Instant::now() < deadline {
        while let Ok(event) = room.try_recv() {
            println!("{event:?}");
        }
        thread::sleep(Duration::from_millis(20));
    }
    println!("Dropped events: {}", room.dropped_events());
    room.disconnect()?;
    Ok(())
}
