use super::*;
use std::cell::RefCell;

owned_handle!(
    TextWriter,
    ffi::lk_text_stream_writer_t,
    ffi::lk_text_stream_writer_destroy,
    callbacks: Option<Box<WriterCallbacks>>
);
owned_handle!(
    ByteWriter,
    ffi::lk_byte_stream_writer_t,
    ffi::lk_byte_stream_writer_destroy,
    callbacks: Option<Box<WriterCallbacks>>
);
macro_rules! writer {
    ($name:ident, $close:path, $cancel:path, $id:path, $closed:path) => {
        impl $name {
            pub(super) fn sdk(&self) -> &Sdk {
                &self._sdk
            }
            pub(super) fn retain_callbacks(mut self, callbacks: Box<WriterCallbacks>) -> Self {
                self.callbacks = Some(callbacks);
                self
            }
            pub fn close(&mut self) -> Result<()> {
                check(unsafe { $close(self.as_raw()) })
            }
            pub fn cancel(&mut self, reason: &str) -> Result<()> {
                let reason = cstring(reason)?;
                check(unsafe { $cancel(self.as_raw(), reason.as_ptr()) })
            }
            pub fn id(&self) -> Result<String> {
                read_string(|p, n| unsafe { $id(self.as_raw(), p, n) })
            }
            pub fn is_closed(&self) -> bool {
                unsafe { $closed(self.as_raw()) != 0 }
            }
        }
    };
}
writer!(
    TextWriter,
    ffi::lk_text_stream_writer_close,
    ffi::lk_text_stream_writer_cancel,
    ffi::lk_text_stream_writer_id,
    ffi::lk_text_stream_writer_is_closed
);
writer!(
    ByteWriter,
    ffi::lk_byte_stream_writer_close,
    ffi::lk_byte_stream_writer_cancel,
    ffi::lk_byte_stream_writer_id,
    ffi::lk_byte_stream_writer_is_closed
);
impl TextWriter {
    pub fn write(&mut self, text: &str) -> Result<()> {
        check(unsafe {
            ffi::lk_text_stream_writer_write(self.as_raw(), text.as_ptr().cast(), text.len())
        })
    }
}
pub(super) struct WriterCallbacks {
    pub events: CallbackState,
    pub stream_id: String,
}
impl ByteWriter {
    pub fn write(&mut self, bytes: &[u8]) -> Result<()> {
        check(unsafe {
            ffi::lk_byte_stream_writer_write(self.as_raw(), bytes.as_ptr(), bytes.len())
        })
    }
}
impl Room {
    pub fn stream_text(&self, topic: &str) -> Result<TextWriter> {
        self.stream_text_with_options(&DataStreamOptions {
            topic: topic.into(),
            ..DataStreamOptions::default()
        })
    }
    pub fn stream_bytes(&self, topic: &str, name: &str, mime_type: &str) -> Result<ByteWriter> {
        self.stream_bytes_with_options(&DataStreamOptions {
            topic: topic.into(),
            name: name.into(),
            mime_type: mime_type.into(),
            ..DataStreamOptions::default()
        })
    }
    pub fn send_file(&self, path: &str, topic: &str) -> Result<()> {
        let path = cstring(path)?;
        let topic = cstring(topic)?;
        let mut o = native!(ffi::lk_file_send_options_init);
        o.topic = topic.as_ptr();
        check(unsafe { ffi::lk_room_send_file(self.as_raw(), path.as_ptr(), &o) })
    }
    pub fn set_attributes(&self, attributes: &[(String, String)]) -> Result<()> {
        let strings = attributes
            .iter()
            .map(|(k, v)| Ok((cstring(k)?, cstring(v)?)))
            .collect::<Result<Vec<_>>>()?;
        let attributes: Vec<_> = strings
            .iter()
            .map(|(k, v)| ffi::lk_attribute_t {
                key: k.as_ptr(),
                value: v.as_ptr(),
            })
            .collect();
        check(unsafe {
            ffi::lk_local_participant_set_attributes(
                self.as_raw(),
                attributes.as_ptr(),
                attributes.len(),
            )
        })
    }
    pub fn send_chat_message(&self, message: &str) -> Result<ChatMessage> {
        let message = cstring(message)?;
        let mut id = vec![0u8; 1024];
        let mut timestamp = 0;
        check(unsafe {
            ffi::lk_room_send_chat_message(
                self.as_raw(),
                message.as_ptr(),
                id.as_mut_ptr().cast(),
                id.len(),
                &mut timestamp,
            )
        })?;
        let end = id
            .iter()
            .position(|&c| c == 0)
            .ok_or_else(|| invalid("unterminated chat message ID"))?;
        Ok(ChatMessage {
            id: String::from_utf8_lossy(&id[..end]).into_owned(),
            timestamp,
        })
    }
    pub fn edit_chat_message(&self, message: &ChatMessage, text: &str) -> Result<()> {
        let id = cstring(&message.id)?;
        let text = cstring(text)?;
        check(unsafe {
            ffi::lk_room_edit_chat_message(
                self.as_raw(),
                id.as_ptr(),
                message.timestamp,
                text.as_ptr(),
            )
        })
    }
    pub fn perform_rpc(
        &self,
        destination: &str,
        method: &str,
        payload: &str,
        timeout_ms: u32,
    ) -> Result<RpcResponse> {
        let d = cstring(destination)?;
        let m = cstring(method)?;
        let p = cstring(payload)?;
        let mut o = native!(ffi::lk_rpc_perform_options_init);
        o.destination_identity = d.as_ptr();
        o.method = m.as_ptr();
        o.payload = p.as_ptr();
        o.response_timeout_ms = timeout_ms;
        let mut raw = ptr::null_mut();
        check(unsafe { ffi::lk_room_perform_rpc(self.as_raw(), &o, &mut raw) })?;
        let result = RpcResult::from_ptr(raw, &self._sdk)?;
        unsafe {
            if ffi::lk_rpc_result_ok(raw) != 0 {
                Ok(RpcResponse::Success(read_string(|p, n| {
                    ffi::lk_rpc_result_payload(result.as_raw(), p, n)
                })?))
            } else {
                Ok(RpcResponse::Failure {
                    code: ffi::lk_rpc_result_error_code(raw),
                    message: read_string(|p, n| ffi::lk_rpc_result_error_message(raw, p, n))?,
                    data: read_string(|p, n| ffi::lk_rpc_result_error_data(raw, p, n))?,
                })
            }
        }
    }
    /// Handlers run on SDK threads and must return quickly. Context is retained
    /// through room destruction, including after unregistering a method.
    pub fn register_rpc_method(
        &mut self,
        method: &str,
        handler: impl Fn(RpcInvocation) -> RpcResponse + Send + Sync + 'static,
    ) -> Result<()> {
        let method = cstring(method)?;
        let mut state = Box::new(RpcHandlerState {
            handler: Box::new(handler),
        });
        check(unsafe {
            ffi::lk_room_register_rpc_method_out(
                self.as_raw(),
                method.as_ptr(),
                Some(on_rpc),
                (&mut *state as *mut RpcHandlerState).cast(),
            )
        })?;
        self.rpc_handlers.push(state);
        Ok(())
    }
    pub fn unregister_rpc_method(&mut self, method: &str) -> Result<()> {
        let method = cstring(method)?;
        check(unsafe { ffi::lk_room_unregister_rpc_method(self.as_raw(), method.as_ptr()) })
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ChatMessage {
    pub id: String,
    pub timestamp: i64,
}
owned_handle!(RpcResult, ffi::lk_rpc_result_t, ffi::lk_rpc_result_destroy);
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RpcResponse {
    Success(String),
    Failure {
        code: u32,
        message: String,
        data: String,
    },
}
#[derive(Debug, Clone)]
pub struct RpcInvocation {
    pub request_id: String,
    pub caller_identity: String,
    pub payload: String,
    pub response_timeout_ms: u32,
}
pub(super) struct RpcHandlerState {
    handler: Box<dyn Fn(RpcInvocation) -> RpcResponse + Send + Sync>,
}
struct ResponseStorage {
    payload: CString,
    message: CString,
    data: CString,
}
thread_local! { static RPC_RESPONSE: RefCell<Option<ResponseStorage>> = const { RefCell::new(None) }; }
unsafe extern "C" fn on_rpc(
    user: *mut c_void,
    invocation: *const ffi::lk_rpc_invocation_t,
    output: *mut ffi::lk_rpc_handler_result_t,
) {
    // C++ copies returned strings immediately after this callback returns, on
    // the same thread. TLS keeps them valid through that copy without a race
    // with simultaneous calls on other native threads.
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| -> Result<()> {
        let state = &*user.cast::<RpcHandlerState>();
        let i = &*invocation;
        let response = (state.handler)(RpcInvocation {
            request_id: copy_string(i.request_id),
            caller_identity: copy_string(i.caller_identity),
            payload: copy_string(i.payload),
            response_timeout_ms: i.response_timeout_ms,
        });
        let (code, payload, message, data) = match response {
            RpcResponse::Success(p) => (0, p, String::new(), String::new()),
            RpcResponse::Failure {
                code,
                message,
                data,
            } => (
                if code == 0 {
                    ffi::LK_RPC_ERROR_APPLICATION_ERROR as u32
                } else {
                    code
                },
                String::new(),
                message,
                data,
            ),
        };
        let storage = ResponseStorage {
            payload: cstring(&payload)?,
            message: cstring(&message)?,
            data: cstring(&data)?,
        };
        RPC_RESPONSE.with(|slot| {
            *output = ffi::lk_rpc_handler_result_t {
                payload: storage.payload.as_ptr(),
                error_code: code,
                error_message: storage.message.as_ptr(),
                error_data: storage.data.as_ptr(),
            };
            *slot.borrow_mut() = Some(storage);
        });
        Ok(())
    }));
    if !matches!(result, Ok(Ok(()))) {
        *output = ffi::lk_rpc_handler_result_t {
            payload: ptr::null(),
            error_code: ffi::LK_RPC_ERROR_APPLICATION_ERROR as u32,
            error_message: c"Rust RPC handler failed".as_ptr(),
            error_data: ptr::null(),
        };
    }
}

pub(super) fn check_data(code: ffi::lk_data_track_error_code_t) -> Result<()> {
    if code == ffi::LK_DATA_TRACK_ERROR_NONE {
        Ok(())
    } else {
        Err(Error {
            status: ffi::LK_STATUS_OPERATION_FAILED,
            message: format!("DataTrack error {code}: {}", unsafe {
                copy_string(ffi::lk_last_error())
            }),
        })
    }
}
pub(super) fn read_bytes(mut getter: impl FnMut(*mut u8, usize) -> usize) -> Result<Vec<u8>> {
    let mut n = getter(ptr::null_mut(), 0);
    check_byte_size(n)?;
    for _ in 0..8 {
        let mut data = vec![0; n];
        let needed = getter(data.as_mut_ptr(), n);
        check_byte_size(needed)?;
        if needed <= n {
            data.truncate(needed);
            return Ok(data);
        }
        n = needed;
    }
    Err(invalid("native byte value changed repeatedly"))
}

fn check_byte_size(size: usize) -> Result<()> {
    if size != 0 {
        return Ok(());
    }
    let mut info = native!(ffi::lk_error_info_init);
    check(unsafe { ffi::lk_last_error_info(&mut info) })?;
    if info.domain == ffi::LK_ERROR_DOMAIN_NONE {
        Ok(())
    } else {
        check(ffi::LK_STATUS_OPERATION_FAILED)
    }
}
#[derive(Debug, Clone)]
pub struct SchemaId {
    pub name: String,
    pub encoding: ffi::lk_data_track_schema_encoding_t,
    pub custom_encoding: String,
}
impl SchemaId {
    pub(super) fn with_native<T>(
        &self,
        f: impl FnOnce(&ffi::lk_data_track_schema_id_t) -> Result<T>,
    ) -> Result<T> {
        let name = cstring(&self.name)?;
        let custom = cstring(&self.custom_encoding)?;
        let mut id = native!(ffi::lk_data_track_schema_id_init);
        id.name = name.as_ptr();
        id.encoding = self.encoding;
        id.custom_encoding = custom.as_ptr();
        f(&id)
    }
}
owned_handle!(
    DataTrackHandle,
    ffi::lk_local_data_track_t,
    ffi::lk_local_data_track_destroy,
    room: Option<Rc<RoomLifetime>>
);
#[derive(Clone)]
pub struct DataTrack(pub(super) Rc<DataTrackHandle>);
impl DataTrack {
    pub(super) fn from_ptr(raw: *mut ffi::lk_local_data_track_t, room: &Room) -> Result<Self> {
        let mut handle = DataTrackHandle::from_ptr(raw, &room._sdk)?;
        handle.room = Some(room.lifetime.clone());
        Ok(Self(Rc::new(handle)))
    }
    pub fn as_raw(&self) -> *mut ffi::lk_local_data_track_t {
        self.0.as_raw()
    }
    pub fn push(&self, bytes: &[u8], timestamp: Option<u64>) -> Result<()> {
        check_data(unsafe {
            ffi::lk_local_data_track_try_push(
                self.as_raw(),
                bytes.as_ptr(),
                bytes.len(),
                timestamp.is_some().into(),
                timestamp.unwrap_or(0),
            )
        })
    }
    pub fn unpublish(&self) -> Result<()> {
        check_data(unsafe { ffi::lk_local_data_track_unpublish(self.as_raw()) })
    }
    pub fn is_published(&self) -> bool {
        unsafe { ffi::lk_local_data_track_is_published(self.as_raw()) != 0 }
    }
    pub fn sid(&self) -> Result<String> {
        read_string(|p, n| unsafe { ffi::lk_local_data_track_sid(self.as_raw(), p, n) })
    }
}
owned_handle!(
    DataSchema,
    ffi::lk_data_track_schema_t,
    ffi::lk_data_track_schema_destroy
);
impl DataSchema {
    pub fn name(&self) -> Result<String> {
        read_string(|p, n| unsafe { ffi::lk_data_track_schema_name(self.as_raw(), p, n) })
    }
    pub fn definition(&self) -> Result<Vec<u8>> {
        read_bytes(|p, n| unsafe { ffi::lk_data_track_schema_definition(self.as_raw(), p, n) })
    }
    pub fn encoding(&self) -> ffi::lk_data_track_schema_encoding_t {
        unsafe { ffi::lk_data_track_schema_encoding(self.as_raw()) }
    }
}
owned_handle!(
    DataReader,
    ffi::lk_data_track_reader_t,
    ffi::lk_data_track_reader_destroy
);
owned_handle!(
    DataFrame,
    ffi::lk_data_track_frame_t,
    ffi::lk_data_track_frame_destroy
);
impl DataFrame {
    pub fn bytes(&self) -> Result<Vec<u8>> {
        read_bytes(|p, n| unsafe { ffi::lk_data_track_frame_data(self.as_raw(), p, n) })
    }
    pub fn timestamp(&self) -> Option<u64> {
        unsafe {
            (ffi::lk_data_track_frame_has_user_timestamp(self.as_raw()) != 0)
                .then(|| ffi::lk_data_track_frame_user_timestamp(self.as_raw()))
        }
    }
}
impl DataReader {
    pub fn read_for(&mut self, timeout_ms: u32) -> Result<Read<DataFrame>> {
        let mut raw = ptr::null_mut();
        match unsafe { ffi::lk_data_track_reader_read_for(self.as_raw(), timeout_ms, &mut raw) } {
            ffi::LK_DATA_TRACK_READ_FRAME => Ok(Read::Frame(DataFrame::from_ptr(raw, &self._sdk)?)),
            ffi::LK_DATA_TRACK_READ_EMPTY => Ok(Read::Empty),
            ffi::LK_DATA_TRACK_READ_CLOSED => Ok(Read::Closed),
            _ => {
                check(ffi::LK_STATUS_OPERATION_FAILED)?;
                unreachable!()
            }
        }
    }
    pub fn close(&mut self) {
        unsafe { ffi::lk_data_track_reader_close(self.as_raw()) }
    }
    pub fn is_closed(&self) -> bool {
        unsafe { ffi::lk_data_track_reader_is_closed(self.as_raw()) != 0 }
    }
    pub fn dropped_frames(&self) -> usize {
        unsafe { ffi::lk_data_track_reader_dropped_frames(self.as_raw()) }
    }
}
impl Room {
    pub fn store_data_schema(&self, id: &SchemaId, definition: &[u8]) -> Result<()> {
        id.with_native(|id| {
            check_data(unsafe {
                ffi::lk_room_store_data_track_schema(
                    self.as_raw(),
                    id,
                    definition.as_ptr(),
                    definition.len(),
                )
            })
        })
    }
    pub fn data_schema(&self, participant: &str, id: &SchemaId) -> Result<DataSchema> {
        let participant = cstring(participant)?;
        id.with_native(|id| {
            let mut raw = ptr::null_mut();
            check_data(unsafe {
                ffi::lk_room_get_data_track_schema(
                    self.as_raw(),
                    participant.as_ptr(),
                    id,
                    &mut raw,
                )
            })?;
            DataSchema::from_ptr(raw, &self._sdk)
        })
    }
    pub fn publish_data_track(
        &mut self,
        name: &str,
        schema: Option<&SchemaId>,
    ) -> Result<DataTrack> {
        let name = cstring(name)?;
        let mut options = native!(ffi::lk_data_track_publish_options_init);
        options.name = name.as_ptr();
        let mut publish = |id: Option<&ffi::lk_data_track_schema_id_t>| {
            if let Some(id) = id {
                options.has_schema = 1;
                options.schema = *id;
            }
            let mut raw = ptr::null_mut();
            check_data(unsafe {
                ffi::lk_room_publish_data_track(self.as_raw(), &options, &mut raw)
            })?;
            let track = DataTrack::from_ptr(raw, self)?;
            self.data_tracks.push(track.clone());
            Ok(track)
        };
        if let Some(schema) = schema {
            schema.with_native(|id| publish(Some(id)))
        } else {
            publish(None)
        }
    }
    pub fn subscribe_data_track(
        &self,
        participant: &str,
        track: &str,
        capacity: usize,
    ) -> Result<DataReader> {
        if capacity == 0 {
            return Err(invalid("capacity must be positive"));
        }
        let p = cstring(participant)?;
        let t = cstring(track)?;
        let mut o = native!(ffi::lk_data_track_subscription_options_init);
        o.buffer_capacity = capacity;
        let mut raw = ptr::null_mut();
        check_data(unsafe {
            ffi::lk_room_subscribe_data_track(self.as_raw(), p.as_ptr(), t.as_ptr(), &o, &mut raw)
        })?;
        DataReader::from_ptr(raw, &self._sdk)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rpc_response_storage_and_panic_boundary() {
        let mut state = RpcHandlerState {
            handler: Box::new(|i| RpcResponse::Success(format!("echo:{}", i.payload))),
        };
        let invocation = ffi::lk_rpc_invocation_t {
            request_id: c"request".as_ptr(),
            caller_identity: c"caller".as_ptr(),
            payload: c"payload".as_ptr(),
            response_timeout_ms: 100,
        };
        let mut result = MaybeUninit::uninit();
        unsafe {
            on_rpc(
                (&mut state as *mut RpcHandlerState).cast(),
                &invocation,
                result.as_mut_ptr(),
            );
            let result = result.assume_init();
            assert_eq!(result.error_code, 0);
            assert_eq!(copy_string(result.payload), "echo:payload");
        }
        state.handler = Box::new(|_| panic!("test RPC panic"));
        unsafe {
            on_rpc(
                (&mut state as *mut RpcHandlerState).cast(),
                &invocation,
                result.as_mut_ptr(),
            );
            assert_ne!(result.assume_init().error_code, 0);
        }
        state.handler = Box::new(|_| RpcResponse::Success("invalid\0value".into()));
        unsafe {
            on_rpc(
                (&mut state as *mut RpcHandlerState).cast(),
                &invocation,
                result.as_mut_ptr(),
            );
            assert_ne!(result.assume_init().error_code, 0);
        }
    }
}
