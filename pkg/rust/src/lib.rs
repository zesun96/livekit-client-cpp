//! Rust bindings for the LiveKit C++ client.
//!
//! [`ffi`] exposes the complete C ABI. [`Sdk`] and [`Room`] provide synchronous,
//! owned room operations and bounded, copied callback events. Rooms stay on the
//! creating thread; poll events there instead of running user code on SDK threads.

#![doc = include_str!("../README.md")]

pub mod ffi;

use std::{
    ffi::{c_char, c_void, CStr, CString},
    fmt,
    marker::PhantomData,
    mem::MaybeUninit,
    ptr::{self, NonNull},
    rc::Rc,
    sync::{
        atomic::{AtomicUsize, Ordering},
        mpsc::{self, Receiver, SyncSender, TryRecvError},
        Arc, Mutex,
    },
};

/// An owned copy of a native failure, captured before another C call can erase it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Error {
    pub status: ffi::lk_status_t,
    pub message: String,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} (status {})", self.message, self.status)
    }
}
impl std::error::Error for Error {}
pub type Result<T> = std::result::Result<T, Error>;

fn invalid(message: &str) -> Error {
    Error {
        status: ffi::LK_STATUS_INVALID_ARGUMENT,
        message: message.into(),
    }
}

fn cstring(value: &str) -> Result<CString> {
    CString::new(value).map_err(|_| invalid("string contains an interior NUL"))
}

// Caller guarantees a native borrowed, NUL-terminated string, or NULL.
unsafe fn copy_string(value: *const c_char) -> String {
    if value.is_null() {
        String::new()
    } else {
        CStr::from_ptr(value).to_string_lossy().into_owned()
    }
}

fn check(status: ffi::lk_status_t) -> Result<()> {
    if status == ffi::LK_STATUS_OK {
        Ok(())
    } else {
        Err(Error {
            status,
            message: unsafe { copy_string(ffi::lk_last_error()) },
        })
    }
}

fn read_string(mut getter: impl FnMut(*mut c_char, usize) -> usize) -> Result<String> {
    let mut size = getter(ptr::null_mut(), 0);
    // Room metadata can change between calls. Retry growth without exposing a
    // truncated result or reading beyond the supplied allocation.
    for _ in 0..8 {
        if size == 0 {
            return Err(Error {
                status: ffi::LK_STATUS_OPERATION_FAILED,
                message: unsafe { copy_string(ffi::lk_last_error()) },
            });
        }
        let mut buffer = vec![0u8; size];
        let needed = getter(buffer.as_mut_ptr().cast(), buffer.len());
        if needed > size {
            size = needed;
            continue;
        }
        if needed == 0 {
            size = 0;
            continue;
        }
        let end = buffer
            .iter()
            .position(|&c| c == 0)
            .ok_or_else(|| invalid("native string is not terminated"))?;
        return Ok(String::from_utf8_lossy(&buffer[..end]).into_owned());
    }
    Err(Error {
        status: ffi::LK_STATUS_OPERATION_FAILED,
        message: "native string changed repeatedly while reading".into(),
    })
}

static RUNTIME_USERS: Mutex<usize> = Mutex::new(0);
struct Runtime;

impl Drop for Runtime {
    fn drop(&mut self) {
        let mut users = RUNTIME_USERS.lock().unwrap_or_else(|e| e.into_inner());
        *users -= 1;
        if *users == 0 {
            unsafe {
                ffi::lk_shutdown();
            }
        }
    }
}

/// Initializes the process SDK. Clones and rooms retain initialization until the
/// last owner is dropped. Do not mix this lifecycle with manual FFI init/shutdown.
#[derive(Clone)]
pub struct Sdk {
    _runtime: Arc<Runtime>,
}

impl Sdk {
    pub fn new() -> Result<Self> {
        let mut users = RUNTIME_USERS.lock().unwrap_or_else(|e| e.into_inner());
        if *users == 0 {
            check(unsafe { ffi::lk_init() })?;
        }
        *users += 1;
        Ok(Self {
            _runtime: Arc::new(Runtime),
        })
    }

    pub fn version(&self) -> Result<String> {
        read_string(|p, n| unsafe { ffi::lk_version(p, n) })
    }

    pub fn room(&self) -> Result<Room> {
        self.room_with_event_capacity(256)
    }

    /// When the queue is full new events are discarded. Use Room::dropped_events
    /// to detect overflow. Callbacks never block waiting for the consumer.
    pub fn room_with_event_capacity(&self, capacity: usize) -> Result<Room> {
        if capacity == 0 {
            return Err(invalid("event capacity must be positive"));
        }
        let (sender, receiver) = mpsc::sync_channel(capacity);
        let callbacks = Box::new(CallbackState {
            sender,
            dropped: AtomicUsize::new(0),
        });
        let mut raw = ptr::null_mut();
        check(unsafe { ffi::lk_room_create(&mut raw) })?;
        let handle = NonNull::new(raw).ok_or_else(|| invalid("native room is null"))?;
        let room = Room {
            handle,
            callbacks,
            receiver,
            _sdk: self.clone(),
            _thread: PhantomData,
        };
        unsafe {
            let mut native = MaybeUninit::uninit();
            ffi::lk_room_callbacks_init(native.as_mut_ptr());
            let mut native = native.assume_init();
            native.user_data = (&*room.callbacks as *const CallbackState).cast_mut().cast();
            native.on_connection_state_changed = Some(on_state);
            native.on_participant_connected = Some(on_participant_connected);
            native.on_participant_disconnected = Some(on_participant_disconnected);
            native.on_data_received = Some(on_data);
            native.on_text_received = Some(on_text);
            check(ffi::lk_room_set_callbacks(raw, &native))?;
        }
        Ok(room)
    }
}

/// Common connection settings. Pointer-bearing ICE, E2EE and token-source
/// options remain available through the unsafe C ABI.
#[derive(Debug, Clone)]
pub struct ConnectOptions {
    pub auto_subscribe: bool,
    pub adaptive_stream: bool,
    pub dynacast: bool,
    pub join_retries: u32,
    pub reconnect_timeout_ms: u32,
}

impl Default for ConnectOptions {
    fn default() -> Self {
        unsafe {
            let mut options = MaybeUninit::uninit();
            ffi::lk_room_connect_options_init(options.as_mut_ptr());
            let options = options.assume_init();
            Self {
                auto_subscribe: options.auto_subscribe != 0,
                adaptive_stream: options.adaptive_stream != 0,
                dynacast: options.dynacast != 0,
                join_retries: options.join_retries,
                reconnect_timeout_ms: options.reconnect_timeout_ms,
            }
        }
    }
}

/// Owned participant strings; independent of room and snapshot lifetimes.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Participant {
    pub sid: String,
    pub identity: String,
    pub name: String,
    pub metadata: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Event {
    ConnectionStateChanged(ffi::lk_room_state_t),
    ParticipantConnected(Participant),
    ParticipantDisconnected(Participant),
    Data {
        data: Vec<u8>,
        topic: String,
        participant_identity: String,
        reliable: bool,
    },
    Text {
        text: String,
        topic: String,
        participant_identity: String,
        stream_id: String,
    },
}

struct CallbackState {
    sender: SyncSender<Event>,
    dropped: AtomicUsize,
}

// No user code runs on a native callback thread. Keep the box alive until native
// destruction has waited for in-flight callbacks to finish.
unsafe fn emit(user_data: *mut c_void, event: impl FnOnce() -> Event) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let state = &*user_data.cast::<CallbackState>();
        if state.sender.try_send(event()).is_err() {
            state.dropped.fetch_add(1, Ordering::Relaxed);
        }
    }));
}

unsafe extern "C" fn on_state(
    user: *mut c_void,
    _: *mut ffi::lk_room_t,
    state: ffi::lk_room_state_t,
) {
    emit(user, || Event::ConnectionStateChanged(state));
}

unsafe fn participant(info: *const ffi::lk_participant_info_t) -> Participant {
    let info = &*info;
    Participant {
        sid: copy_string(info.sid),
        identity: copy_string(info.identity),
        name: copy_string(info.name),
        metadata: copy_string(info.metadata),
    }
}

unsafe extern "C" fn on_participant_connected(
    user: *mut c_void,
    _: *mut ffi::lk_room_t,
    info: *const ffi::lk_participant_info_t,
) {
    emit(user, || Event::ParticipantConnected(participant(info)));
}
unsafe extern "C" fn on_participant_disconnected(
    user: *mut c_void,
    _: *mut ffi::lk_room_t,
    info: *const ffi::lk_participant_info_t,
) {
    emit(user, || Event::ParticipantDisconnected(participant(info)));
}
unsafe extern "C" fn on_data(
    user: *mut c_void,
    _: *mut ffi::lk_room_t,
    info: *const ffi::lk_data_received_t,
) {
    emit(user, || {
        let info = &*info;
        let data = if info.data_size == 0 {
            Vec::new()
        } else {
            std::slice::from_raw_parts(info.data, info.data_size).to_vec()
        };
        Event::Data {
            data,
            topic: copy_string(info.topic),
            participant_identity: copy_string(info.participant_identity),
            reliable: info.reliable != 0,
        }
    });
}
unsafe extern "C" fn on_text(
    user: *mut c_void,
    _: *mut ffi::lk_room_t,
    info: *const ffi::lk_text_received_t,
) {
    emit(user, || {
        let info = &*info;
        Event::Text {
            text: copy_string(info.text),
            topic: copy_string(info.topic),
            participant_identity: copy_string(info.participant_identity),
            stream_id: copy_string(info.stream_id),
        }
    });
}

/// One native room. Neither Send nor Sync: use and drop it on its owning thread.
/// Drop disconnects/destroys the native room before releasing callback storage.
pub struct Room {
    handle: NonNull<ffi::lk_room_t>,
    callbacks: Box<CallbackState>,
    receiver: Receiver<Event>,
    _sdk: Sdk,
    _thread: PhantomData<Rc<()>>,
}

impl Drop for Room {
    fn drop(&mut self) {
        unsafe { ffi::lk_room_destroy(self.handle.as_ptr()) }
    }
}

impl Room {
    /// Borrowed handle for advanced unsafe FFI operations. Never destroy it or
    /// replace its callback table while using the safe wrapper.
    pub fn as_raw(&self) -> *mut ffi::lk_room_t {
        self.handle.as_ptr()
    }

    pub fn try_recv(&self) -> std::result::Result<Event, TryRecvError> {
        self.receiver.try_recv()
    }
    pub fn dropped_events(&self) -> usize {
        self.callbacks.dropped.load(Ordering::Relaxed)
    }

    pub fn connect(&mut self, url: &str, token: &str) -> Result<()> {
        self.connect_with_options(url, token, &ConnectOptions::default())
    }

    pub fn connect_with_options(
        &mut self,
        url: &str,
        token: &str,
        options: &ConnectOptions,
    ) -> Result<()> {
        let url = cstring(url)?;
        let token = cstring(token)?;
        unsafe {
            let mut native = MaybeUninit::uninit();
            ffi::lk_room_connect_options_init(native.as_mut_ptr());
            let mut native = native.assume_init();
            native.auto_subscribe = options.auto_subscribe.into();
            native.adaptive_stream = options.adaptive_stream.into();
            native.dynacast = options.dynacast.into();
            native.join_retries = options.join_retries;
            native.reconnect_timeout_ms = options.reconnect_timeout_ms;
            check(ffi::lk_room_connect_with_options(
                self.as_raw(),
                url.as_ptr(),
                token.as_ptr(),
                &native,
            ))
        }
    }

    pub fn disconnect(&mut self) -> Result<()> {
        check(unsafe { ffi::lk_room_disconnect(self.as_raw()) })
    }
    pub fn is_connected(&self) -> bool {
        unsafe { ffi::lk_room_is_connected(self.as_raw()) != 0 }
    }
    pub fn state(&self) -> ffi::lk_room_state_t {
        unsafe { ffi::lk_room_state(self.as_raw()) }
    }
    pub fn disconnect_reason(&self) -> ffi::lk_disconnect_reason_t {
        unsafe { ffi::lk_room_disconnect_reason(self.as_raw()) }
    }
    pub fn sid(&self) -> Result<String> {
        read_string(|p, n| unsafe { ffi::lk_room_sid(self.as_raw(), p, n) })
    }
    pub fn name(&self) -> Result<String> {
        read_string(|p, n| unsafe { ffi::lk_room_name(self.as_raw(), p, n) })
    }
    pub fn metadata(&self) -> Result<String> {
        read_string(|p, n| unsafe { ffi::lk_room_metadata(self.as_raw(), p, n) })
    }

    pub fn set_name(&mut self, name: &str) -> Result<()> {
        let name = cstring(name)?;
        check(unsafe { ffi::lk_local_participant_set_name(self.as_raw(), name.as_ptr()) })
    }
    pub fn set_metadata(&mut self, metadata: &str) -> Result<()> {
        let metadata = cstring(metadata)?;
        check(unsafe { ffi::lk_local_participant_set_metadata(self.as_raw(), metadata.as_ptr()) })
    }

    pub fn publish_data(&mut self, data: &[u8], topic: &str, reliable: bool) -> Result<()> {
        let topic = cstring(topic)?;
        unsafe {
            let mut options = MaybeUninit::uninit();
            ffi::lk_data_publish_options_init(options.as_mut_ptr());
            let mut options = options.assume_init();
            options.topic = topic.as_ptr();
            options.reliable = reliable.into();
            check(ffi::lk_room_publish_data(
                self.as_raw(),
                data.as_ptr(),
                data.len(),
                &options,
            ))
        }
    }

    pub fn send_text(&mut self, text: &str, topic: &str) -> Result<()> {
        let text = cstring(text)?;
        let topic = cstring(topic)?;
        unsafe {
            let mut options = MaybeUninit::uninit();
            ffi::lk_text_send_options_init(options.as_mut_ptr());
            let mut options = options.assume_init();
            options.topic = topic.as_ptr();
            check(ffi::lk_room_send_text(
                self.as_raw(),
                text.as_ptr(),
                &options,
            ))
        }
    }

    pub fn send_bytes(&mut self, data: &[u8], topic: &str) -> Result<()> {
        let topic = cstring(topic)?;
        unsafe {
            let mut options = MaybeUninit::uninit();
            ffi::lk_byte_send_options_init(options.as_mut_ptr());
            let mut options = options.assume_init();
            options.topic = topic.as_ptr();
            check(ffi::lk_room_send_bytes(
                self.as_raw(),
                data.as_ptr(),
                data.len(),
                &options,
            ))
        }
    }

    pub fn local_participant(&self) -> Result<Participant> {
        unsafe {
            let mut raw = ptr::null_mut();
            check(ffi::lk_room_create_local_participant_snapshot(
                self.as_raw(),
                &mut raw,
            ))?;
            let _snapshot = LocalSnapshot(raw);
            Ok(Participant {
                sid: read_string(|p, n| ffi::lk_local_participant_snapshot_sid(raw, p, n))?,
                identity: read_string(|p, n| {
                    ffi::lk_local_participant_snapshot_identity(raw, p, n)
                })?,
                name: read_string(|p, n| ffi::lk_local_participant_snapshot_name(raw, p, n))?,
                metadata: read_string(|p, n| {
                    ffi::lk_local_participant_snapshot_metadata(raw, p, n)
                })?,
            })
        }
    }

    pub fn remote_participants(&self) -> Result<Vec<Participant>> {
        unsafe {
            let mut raw = ptr::null_mut();
            check(ffi::lk_room_create_remote_participant_snapshot(
                self.as_raw(),
                &mut raw,
            ))?;
            let _snapshot = RemoteSnapshot(raw);
            let mut result = Vec::new();
            for i in 0..ffi::lk_remote_participant_list_count(raw) {
                let mut item = ptr::null();
                check(ffi::lk_remote_participant_list_at(raw, i, &mut item))?;
                result.push(Participant {
                    sid: read_string(|p, n| ffi::lk_remote_participant_snapshot_sid(item, p, n))?,
                    identity: read_string(|p, n| {
                        ffi::lk_remote_participant_snapshot_identity(item, p, n)
                    })?,
                    name: read_string(|p, n| ffi::lk_remote_participant_snapshot_name(item, p, n))?,
                    metadata: read_string(|p, n| {
                        ffi::lk_remote_participant_snapshot_metadata(item, p, n)
                    })?,
                });
            }
            Ok(result)
        }
    }
}

struct LocalSnapshot(*mut ffi::lk_local_participant_snapshot_t);
impl Drop for LocalSnapshot {
    fn drop(&mut self) {
        unsafe { ffi::lk_local_participant_snapshot_destroy(self.0) }
    }
}
struct RemoteSnapshot(*mut ffi::lk_remote_participant_list_t);
impl Drop for RemoteSnapshot {
    fn drop(&mut self) {
        unsafe { ffi::lk_remote_participant_list_destroy(self.0) }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn callback_copies_binary_data_and_reports_overflow() {
        let (sender, receiver) = mpsc::sync_channel(1);
        let state = CallbackState {
            sender,
            dropped: AtomicUsize::new(0),
        };
        let topic = CString::new("测试").unwrap();
        let mut bytes = [1, 0, 255];
        let info = ffi::lk_data_received_t {
            data: bytes.as_ptr(),
            data_size: bytes.len(),
            topic: topic.as_ptr(),
            participant_identity: ptr::null(),
            reliable: 1,
        };
        let user = (&state as *const CallbackState).cast_mut().cast();
        unsafe {
            on_data(user, ptr::null_mut(), &info);
            on_data(user, ptr::null_mut(), &info);
        }
        bytes.fill(42);
        drop(topic);
        assert_eq!(state.dropped.load(Ordering::Relaxed), 1);
        assert_eq!(
            receiver.try_recv().unwrap(),
            Event::Data {
                data: vec![1, 0, 255],
                topic: "测试".into(),
                participant_identity: String::new(),
                reliable: true,
            }
        );
        let empty = ffi::lk_data_received_t {
            data: ptr::null(),
            data_size: 0,
            topic: ptr::null(),
            participant_identity: ptr::null(),
            reliable: 0,
        };
        unsafe {
            on_data(user, ptr::null_mut(), &empty);
        }
        assert!(
            matches!(receiver.try_recv().unwrap(), Event::Data { data, .. } if data.is_empty())
        );
    }

    #[test]
    fn string_reader_retries_growth() {
        let mut calls = 0;
        let value = read_string(|p, n| {
            calls += 1;
            if n == 0 {
                return 2;
            }
            if n < 6 {
                return 6;
            }
            unsafe {
                ptr::copy_nonoverlapping(c"hello".as_ptr(), p, 6);
            }
            6
        })
        .unwrap();
        assert_eq!(value, "hello");
        assert_eq!(calls, 3);
        assert!(cstring("a\0b").is_err());
    }
}
