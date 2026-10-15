use super::*;
use std::{
    future::Future,
    pin::Pin,
    task::{Context, Poll, Waker},
    thread::{self, JoinHandle},
};

struct Reply<T> {
    result: Option<Result<T>>,
    waker: Option<Waker>,
}
struct Completion<T> {
    state: Arc<Mutex<Reply<T>>>,
    delivered: bool,
}

/// A runtime-independent future. Commands begin when submitted. Dropping the
/// future discards its result; it does not cancel a native operation in progress.
/// The owning AsyncRoom joins its worker and drains queued commands on drop.
#[must_use = "await the operation to observe its result"]
pub struct Operation<T> {
    state: Arc<Mutex<Reply<T>>>,
    done: bool,
}
impl<T> Future for Operation<T> {
    type Output = Result<T>;
    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = self.get_mut();
        assert!(!this.done, "operation polled after completion");
        let mut state = this.state.lock().unwrap_or_else(|e| e.into_inner());
        if let Some(result) = state.result.take() {
            this.done = true;
            Poll::Ready(result)
        } else {
            state.waker = Some(cx.waker().clone());
            Poll::Pending
        }
    }
}
impl<T> Completion<T> {
    fn finish(&mut self, result: Result<T>) {
        let waker = {
            let mut state = self.state.lock().unwrap_or_else(|e| e.into_inner());
            state.result = Some(result);
            self.delivered = true;
            state.waker.take()
        };
        if let Some(waker) = waker {
            waker.wake();
        }
    }
}
impl<T> Drop for Completion<T> {
    fn drop(&mut self) {
        if !self.delivered {
            self.finish(Err(Error {
                status: ffi::LK_STATUS_INVALID_STATE,
                message: "async command queue is full or the room worker has stopped".into(),
            }));
        }
    }
}
fn reply<T>() -> (Operation<T>, Completion<T>) {
    let state = Arc::new(Mutex::new(Reply {
        result: None,
        waker: None,
    }));
    (
        Operation {
            state: state.clone(),
            done: false,
        },
        Completion {
            state,
            delivered: false,
        },
    )
}

pub(super) struct EventNotifier {
    waker: Mutex<Option<Waker>>,
}
impl EventNotifier {
    pub(super) fn notify(&self) {
        let waker = self.waker.lock().unwrap_or_else(|e| e.into_inner()).take();
        if let Some(waker) = waker {
            waker.wake();
        }
    }
}

/// Receives the next copied room event without blocking an executor thread.
/// Only one event receive may be pending on a room at a time.
pub struct NextEvent<'a> {
    room: &'a mut AsyncRoom,
}
impl Future for NextEvent<'_> {
    type Output = std::result::Result<Event, mpsc::RecvError>;
    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let room = &self.get_mut().room;
        let mut waker = room
            .notifier
            .waker
            .lock()
            .unwrap_or_else(|e| e.into_inner());
        match room.events.try_recv() {
            Ok(event) => {
                *waker = None;
                Poll::Ready(Ok(event))
            }
            Err(TryRecvError::Disconnected) => {
                *waker = None;
                Poll::Ready(Err(mpsc::RecvError))
            }
            Err(TryRecvError::Empty) => {
                *waker = Some(cx.waker().clone());
                Poll::Pending
            }
        }
    }
}
impl Drop for NextEvent<'_> {
    fn drop(&mut self) {
        self.room
            .notifier
            .waker
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .take();
    }
}

type Job = Box<dyn FnOnce(&mut Room) + Send>;
enum Command {
    Run(Job),
    Stop,
}

/// Cloneable Send + Sync command handle. It can be moved into executor tasks;
/// keep its AsyncRoom owner alive until those tasks have finished. A handle does
/// not keep the native room or worker alive after the owner is dropped.
#[derive(Clone)]
pub struct AsyncRoomHandle {
    sender: SyncSender<Command>,
}

/// Owns a Room on a dedicated, joined worker thread. All native room operations
/// are serialized there; no Room or native owned handle is marked Send.
///
/// Keep this owner on its creating thread. Returned Operation futures contain
/// only copied results and may be awaited by any executor. Drop can block until
/// native operations finish; disconnect().await before dropping for prompt exit.
pub struct AsyncRoom {
    handle: AsyncRoomHandle,
    worker: Option<JoinHandle<()>>,
    events: Receiver<Event>,
    notifier: Arc<EventNotifier>,
    _thread: PhantomData<Rc<()>>,
}
impl Sdk {
    pub fn async_room(&self) -> Result<AsyncRoom> {
        self.async_room_with_capacity(256, 64)
    }
    pub fn async_room_with_capacity(
        &self,
        event_capacity: usize,
        command_capacity: usize,
    ) -> Result<AsyncRoom> {
        if event_capacity == 0 || command_capacity == 0 {
            return Err(invalid("async room capacities must be positive"));
        }
        let sdk = self.clone();
        let (sender, receiver) = mpsc::sync_channel::<Command>(command_capacity);
        let (ready_tx, ready_rx) = mpsc::sync_channel(1);
        let notifier = Arc::new(EventNotifier {
            waker: Mutex::new(None),
        });
        let wake = notifier.clone();
        let worker = thread::Builder::new()
            .name("livekit-rust-room".into())
            .spawn(move || match sdk.room_with_event_capacity(event_capacity) {
                Ok(mut room) => {
                    let (unused, placeholder) = mpsc::sync_channel(0);
                    drop(unused);
                    let events = std::mem::replace(&mut room.receiver, placeholder);
                    *room
                        .callbacks
                        .notifier
                        .lock()
                        .unwrap_or_else(|e| e.into_inner()) = Some(wake.clone());
                    if ready_tx.send(Ok(events)).is_ok() {
                        while let Ok(command) = receiver.recv() {
                            match command {
                                Command::Run(job) => {
                                    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(
                                        || job(&mut room),
                                    ));
                                }
                                Command::Stop => break,
                            }
                        }
                    }
                    drop(room);
                    wake.notify();
                }
                Err(error) => {
                    let _ = ready_tx.send(Err(error));
                }
            })
            .map_err(|e| invalid(&format!("cannot start async room worker: {e}")))?;
        match ready_rx.recv() {
            Ok(Ok(events)) => Ok(AsyncRoom {
                handle: AsyncRoomHandle { sender },
                worker: Some(worker),
                events,
                notifier,
                _thread: PhantomData,
            }),
            result => {
                drop(sender);
                let _ = worker.join();
                Err(match result {
                    Ok(Err(error)) => error,
                    _ => invalid("async room worker initialization failed"),
                })
            }
        }
    }
}
impl Drop for AsyncRoom {
    fn drop(&mut self) {
        let _ = self.handle.sender.send(Command::Stop);
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
    }
}
impl std::ops::Deref for AsyncRoom {
    type Target = AsyncRoomHandle;
    fn deref(&self) -> &Self::Target {
        &self.handle
    }
}
impl AsyncRoomHandle {
    fn submit<T: Send + 'static>(
        &self,
        action: impl FnOnce(&mut Room) -> Result<T> + Send + 'static,
    ) -> Operation<T> {
        let (future, mut completion) = reply();
        let job: Job = Box::new(move |room| {
            let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| action(room)))
                .unwrap_or_else(|_| {
                    Err(Error {
                        status: ffi::LK_STATUS_EXCEPTION,
                        message: "async room operation panicked".into(),
                    })
                });
            completion.finish(result);
        });
        if let Err(error) = self.sender.try_send(Command::Run(job)) {
            // Dropping the rejected job completes its reply with an error.
            drop(error);
        }
        future
    }
    /// Run a synchronous safe operation on the room's worker. The closure and
    /// result must be Send; native handles remain confined to that worker.
    pub fn call<T: Send + 'static>(
        &self,
        action: impl FnOnce(&mut Room) -> Result<T> + Send + 'static,
    ) -> Operation<T> {
        self.submit(action)
    }
    pub fn connect_with_advanced_options(
        &self,
        url: &str,
        token: &str,
        options: &AdvancedConnectOptions,
    ) -> Operation<()> {
        let url = url.to_owned();
        let token = token.to_owned();
        let options = options.clone();
        self.submit(move |room| {
            room.connect_with_advanced_options(&url, &token, &options)?;
            room.wait_connected(options.base.reconnect_timeout_ms)
        })
    }
    pub fn connect_with_token_provider(
        &self,
        options: &AdvancedConnectOptions,
        fetch_options: &TokenFetchOptions,
        fetch: impl Fn(TokenRequest) -> Result<TokenResponse> + Send + Sync + 'static,
    ) -> Operation<()> {
        let options = options.clone();
        let fetch_options = fetch_options.clone();
        self.submit(move |room| {
            room.connect_with_token_provider(&options, &fetch_options, fetch)?;
            room.wait_connected(options.base.reconnect_timeout_ms)
        })
    }
    pub fn publish_data_with_options(
        &self,
        bytes: &[u8],
        options: &DataPublishOptions,
    ) -> Operation<()> {
        let bytes = bytes.to_vec();
        let options = options.clone();
        self.submit(move |room| room.publish_data_with_options(&bytes, &options))
    }
    pub fn send_text_with_options(&self, text: &str, options: &DataStreamOptions) -> Operation<()> {
        let text = text.to_owned();
        let options = options.clone();
        self.submit(move |room| room.send_text_with_options(&text, &options))
    }
    pub fn send_bytes_with_options(
        &self,
        bytes: &[u8],
        options: &DataStreamOptions,
    ) -> Operation<()> {
        let bytes = bytes.to_vec();
        let options = options.clone();
        self.submit(move |room| room.send_bytes_with_options(&bytes, &options))
    }
    pub fn send_file_with_options(&self, path: &str, options: &DataStreamOptions) -> Operation<()> {
        let path = path.to_owned();
        let options = options.clone();
        self.submit(move |room| room.send_file_with_options(&path, &options))
    }
    pub fn connect(&self, url: &str, token: &str) -> Operation<()> {
        self.connect_with_options(url, token, &ConnectOptions::default())
    }
    pub fn connect_with_options(
        &self,
        url: &str,
        token: &str,
        options: &ConnectOptions,
    ) -> Operation<()> {
        let url = url.to_owned();
        let token = token.to_owned();
        let options = options.clone();
        self.submit(move |room| {
            room.connect_with_options(&url, &token, &options)?;
            room.wait_connected(options.reconnect_timeout_ms)
        })
    }
    pub fn connect_with_token_source(
        &self,
        options: &ConnectOptions,
        fetch: impl Fn(bool) -> Result<TokenResponse> + Send + Sync + 'static,
    ) -> Operation<()> {
        let options = options.clone();
        self.submit(move |room| {
            room.connect_with_token_source(&options, fetch)?;
            room.wait_connected(options.reconnect_timeout_ms)
        })
    }
    pub fn disconnect(&self) -> Operation<()> {
        self.submit(|room| room.disconnect())
    }
    pub fn is_connected(&self) -> Operation<bool> {
        self.submit(|room| Ok(room.is_connected()))
    }
    pub fn state(&self) -> Operation<ffi::lk_room_state_t> {
        self.submit(|room| Ok(room.state()))
    }
    pub fn sid(&self) -> Operation<String> {
        self.submit(|room| room.sid())
    }
    pub fn name(&self) -> Operation<String> {
        self.submit(|room| room.name())
    }
    pub fn metadata(&self) -> Operation<String> {
        self.submit(|room| room.metadata())
    }
    pub fn local_participant(&self) -> Operation<Participant> {
        self.submit(|room| room.local_participant())
    }
    pub fn remote_participants(&self) -> Operation<Vec<Participant>> {
        self.submit(|room| room.remote_participants())
    }
    pub fn remote_tracks(&self) -> Operation<Vec<TrackPublication>> {
        self.submit(|room| room.remote_tracks())
    }
    pub fn set_name(&self, name: &str) -> Operation<()> {
        let name = name.to_owned();
        self.submit(move |room| room.set_name(&name))
    }
    pub fn set_metadata(&self, value: &str) -> Operation<()> {
        let value = value.to_owned();
        self.submit(move |room| room.set_metadata(&value))
    }
    pub fn set_attributes(&self, attributes: &[(String, String)]) -> Operation<()> {
        let attributes = attributes.to_vec();
        self.submit(move |room| room.set_attributes(&attributes))
    }
    pub fn publish_data(&self, bytes: &[u8], topic: &str, reliable: bool) -> Operation<()> {
        let bytes = bytes.to_vec();
        let topic = topic.to_owned();
        self.submit(move |room| room.publish_data(&bytes, &topic, reliable))
    }
    pub fn send_text(&self, text: &str, topic: &str) -> Operation<()> {
        let text = text.to_owned();
        let topic = topic.to_owned();
        self.submit(move |room| room.send_text(&text, &topic))
    }
    pub fn send_bytes(&self, bytes: &[u8], topic: &str) -> Operation<()> {
        let bytes = bytes.to_vec();
        let topic = topic.to_owned();
        self.submit(move |room| room.send_bytes(&bytes, &topic))
    }
    pub fn send_file(&self, path: &str, topic: &str) -> Operation<()> {
        let path = path.to_owned();
        let topic = topic.to_owned();
        self.submit(move |room| room.send_file(&path, &topic))
    }
    pub fn send_chat_message(&self, text: &str) -> Operation<ChatMessage> {
        let text = text.to_owned();
        self.submit(move |room| room.send_chat_message(&text))
    }
    pub fn perform_rpc(
        &self,
        destination: &str,
        method: &str,
        payload: &str,
        timeout_ms: u32,
    ) -> Operation<RpcResponse> {
        let d = destination.to_owned();
        let m = method.to_owned();
        let p = payload.to_owned();
        self.submit(move |room| room.perform_rpc(&d, &m, &p, timeout_ms))
    }
    pub fn register_rpc_method(
        &self,
        method: &str,
        handler: impl Fn(RpcInvocation) -> RpcResponse + Send + Sync + 'static,
    ) -> Operation<()> {
        let method = method.to_owned();
        self.submit(move |room| room.register_rpc_method(&method, handler))
    }
    pub fn unregister_rpc_method(&self, method: &str) -> Operation<()> {
        let method = method.to_owned();
        self.submit(move |room| room.unregister_rpc_method(&method))
    }
    pub fn register_text_stream(&self, topic: &str) -> Operation<()> {
        let topic = topic.to_owned();
        self.submit(move |room| room.register_text_stream(&topic))
    }
    pub fn register_byte_stream(&self, topic: &str) -> Operation<()> {
        let topic = topic.to_owned();
        self.submit(move |room| room.register_byte_stream(&topic))
    }
    pub fn dropped_events(&self) -> Operation<usize> {
        self.submit(|room| Ok(room.dropped_events()))
    }
}
impl AsyncRoom {
    pub fn handle(&self) -> AsyncRoomHandle {
        self.handle.clone()
    }
    pub fn try_recv(&self) -> std::result::Result<Event, TryRecvError> {
        self.events.try_recv()
    }
    pub fn next_event(&mut self) -> NextEvent<'_> {
        NextEvent { room: self }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::task::{Wake, Waker};
    struct Count(AtomicUsize);
    impl Wake for Count {
        fn wake(self: Arc<Self>) {
            self.0.fetch_add(1, Ordering::SeqCst);
        }
    }
    #[test]
    fn completion_wakes_and_cancellation_never_loses_a_result() {
        let (mut future, mut completion) = reply::<u32>();
        let count = Arc::new(Count(AtomicUsize::new(0)));
        let waker = Waker::from(count.clone());
        let mut cx = Context::from_waker(&waker);
        assert!(Pin::new(&mut future).poll(&mut cx).is_pending());
        completion.finish(Ok(7));
        assert_eq!(count.0.load(Ordering::SeqCst), 1);
        assert_eq!(Pin::new(&mut future).poll(&mut cx), Poll::Ready(Ok(7)));
        let (mut future, completion) = reply::<u32>();
        drop(completion);
        assert!(matches!(
            Pin::new(&mut future).poll(&mut cx),
            Poll::Ready(Err(_))
        ));
    }
    #[test]
    fn command_overflow_and_event_notification_are_bounded() {
        let sdk = Sdk::new().unwrap();
        let mut room = sdk.async_room_with_capacity(1, 1).unwrap();
        let handle = room.handle();
        let (entered_tx, entered_rx) = mpsc::channel();
        let (release_tx, release_rx) = mpsc::channel();
        let first = handle.call(move |_| {
            entered_tx.send(()).unwrap();
            release_rx.recv().unwrap();
            Ok(())
        });
        entered_rx.recv().unwrap();
        let queued = handle.state();
        let mut rejected = handle.state();
        let count = Arc::new(Count(AtomicUsize::new(0)));
        let waker = Waker::from(count.clone());
        let mut cx = Context::from_waker(&waker);
        assert!(matches!(
            Pin::new(&mut rejected).poll(&mut cx),
            Poll::Ready(Err(_))
        ));
        drop(first);
        release_tx.send(()).unwrap();
        let mut queued = queued;
        loop {
            if let Poll::Ready(value) = Pin::new(&mut queued).poll(&mut cx) {
                value.unwrap();
                break;
            }
            thread::yield_now();
        }
        let mut event = room.next_event();
        assert!(Pin::new(&mut event).poll(&mut cx).is_pending());
        let mut delivered = handle.call(|room| {
            unsafe {
                on_state(
                    (&*room.callbacks as *const CallbackState).cast_mut().cast(),
                    room.as_raw(),
                    ffi::LK_ROOM_STATE_RECONNECTING,
                );
            }
            Ok(())
        });
        loop {
            if let Poll::Ready(value) = Pin::new(&mut delivered).poll(&mut cx) {
                value.unwrap();
                break;
            }
            thread::yield_now();
        }
        assert!(count.0.load(Ordering::SeqCst) > 0);
        assert!(matches!(
            Pin::new(&mut event).poll(&mut cx),
            Poll::Ready(Ok(Event::ConnectionStateChanged(
                ffi::LK_ROOM_STATE_RECONNECTING
            )))
        ));
    }
}
