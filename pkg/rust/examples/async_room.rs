use livekit_client_cpp::*;
use std::{
    env,
    future::Future,
    pin::pin,
    sync::Arc,
    task::{Context, Poll, Wake, Waker},
    thread,
};
struct ThreadWake(thread::Thread);
impl Wake for ThreadWake {
    fn wake(self: Arc<Self>) {
        self.0.unpark();
    }
    fn wake_by_ref(self: &Arc<Self>) {
        self.0.unpark();
    }
}
// A minimal executor for this standalone example; Operation also works with
// Tokio, async-std and other standard Future executors without an SDK feature flag.
fn block_on<F: Future>(future: F) -> F::Output {
    let waker = Waker::from(Arc::new(ThreadWake(thread::current())));
    let mut cx = Context::from_waker(&waker);
    let mut future = pin!(future);
    loop {
        match future.as_mut().poll(&mut cx) {
            Poll::Ready(value) => return value,
            Poll::Pending => thread::park(),
        }
    }
}
async fn run() -> std::result::Result<(), Box<dyn std::error::Error>> {
    let sdk = Sdk::new()?;
    let mut room = sdk.async_room()?;
    room.connect(&env::var("LIVEKIT_URL")?, &env::var("LIVEKIT_TOKEN")?)
        .await?;
    println!("Connected as {}", room.local_participant().await?.identity);
    room.publish_data(b"hello async Rust", "async-example", true)
        .await?;
    room.send_text("Hello async Rust", "async-example").await?;
    // The connected/state event is already queued after connect().await.
    println!("{:?}", room.next_event().await?);
    room.disconnect().await?;
    Ok(())
}
fn main() -> std::result::Result<(), Box<dyn std::error::Error>> {
    block_on(run())
}
