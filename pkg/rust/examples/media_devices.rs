use livekit_client_cpp::Sdk;
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let sdk = Sdk::new()?;
    for device in sdk.media_devices()? {
        println!("{device:?}");
    }
    for screen in sdk.screen_sources()? {
        println!("{screen:?}");
    }
    Ok(())
}
