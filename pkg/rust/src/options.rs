use super::*;

#[derive(Debug, Clone)]
pub struct DataPublishOptions {
    pub reliable: bool,
    pub topic: String,
    pub destination_identities: Vec<String>,
}
impl Default for DataPublishOptions {
    fn default() -> Self {
        let o = native!(ffi::lk_data_publish_options_init);
        Self {
            reliable: o.reliable != 0,
            topic: String::new(),
            destination_identities: Vec::new(),
        }
    }
}
/// Owned options shared by one-shot messages and incremental stream writers.
/// Text uses reply/attachment/update fields; byte streams use name/mime_type.
#[derive(Debug, Clone)]
pub struct DataStreamOptions {
    pub topic: String,
    pub destination_identities: Vec<String>,
    pub attributes: Vec<(String, String)>,
    pub name: String,
    pub mime_type: String,
    pub reply_to_stream_id: String,
    pub attached_stream_ids: Vec<String>,
    pub stream_id: String,
    pub total_size: Option<u64>,
    pub chunk_size: usize,
    pub compress: bool,
    pub update: bool,
    pub version: i32,
}
impl Default for DataStreamOptions {
    fn default() -> Self {
        let o = native!(ffi::lk_stream_text_options_init);
        Self {
            topic: String::new(),
            destination_identities: Vec::new(),
            attributes: Vec::new(),
            name: String::new(),
            mime_type: "application/octet-stream".into(),
            reply_to_stream_id: String::new(),
            attached_stream_ids: Vec::new(),
            stream_id: String::new(),
            total_size: None,
            chunk_size: o.chunk_size,
            compress: o.compress != 0,
            update: false,
            version: o.version,
        }
    }
}
struct Strings {
    topic: CString,
    name: CString,
    mime: CString,
    reply: CString,
    stream: CString,
    destinations: Vec<*const c_char>,
    attributes: Vec<ffi::lk_attribute_t>,
    attachments: Vec<*const c_char>,
    _destinations: Vec<CString>,
    _attributes: Vec<(CString, CString)>,
    _attachments: Vec<CString>,
}
impl Strings {
    fn new(o: &DataStreamOptions) -> Result<Self> {
        let destinations = o
            .destination_identities
            .iter()
            .map(|v| cstring(v))
            .collect::<Result<Vec<_>>>()?;
        let attributes = o
            .attributes
            .iter()
            .map(|(k, v)| Ok((cstring(k)?, cstring(v)?)))
            .collect::<Result<Vec<_>>>()?;
        let attachments = o
            .attached_stream_ids
            .iter()
            .map(|v| cstring(v))
            .collect::<Result<Vec<_>>>()?;
        Ok(Self {
            topic: cstring(&o.topic)?,
            name: cstring(&o.name)?,
            mime: cstring(&o.mime_type)?,
            reply: cstring(&o.reply_to_stream_id)?,
            stream: cstring(&o.stream_id)?,
            destinations: destinations.iter().map(|v| v.as_ptr()).collect(),
            attributes: attributes
                .iter()
                .map(|(k, v)| ffi::lk_attribute_t {
                    key: k.as_ptr(),
                    value: v.as_ptr(),
                })
                .collect(),
            attachments: attachments.iter().map(|v| v.as_ptr()).collect(),
            _destinations: destinations,
            _attributes: attributes,
            _attachments: attachments,
        })
    }
}
macro_rules! common {
    ($n:ident,$s:ident,$o:ident) => {
        $n.topic = $s.topic.as_ptr();
        $n.destination_identities = $s.destinations.as_ptr();
        $n.destination_identity_count = $s.destinations.len();
        $n.attributes = $s.attributes.as_ptr();
        $n.attribute_count = $s.attributes.len();
        $n.chunk_size = $o.chunk_size;
        $n.compress = $o.compress.into();
    };
}
macro_rules! text {
    ($n:ident,$s:ident) => {
        $n.reply_to_stream_id = $s.reply.as_ptr();
        $n.attached_stream_ids = $s.attachments.as_ptr();
        $n.attached_stream_id_count = $s.attachments.len();
    };
}
macro_rules! binary {
    ($n:ident,$s:ident) => {
        $n.name = $s.name.as_ptr();
        $n.mime_type = $s.mime.as_ptr();
    };
}
static NEXT_STREAM: AtomicUsize = AtomicUsize::new(1);
fn stream_options(o: &DataStreamOptions) -> DataStreamOptions {
    let mut o = o.clone();
    if o.stream_id.is_empty() {
        let time = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos();
        o.stream_id = format!(
            "rust-{}-{time:x}-{}",
            std::process::id(),
            NEXT_STREAM.fetch_add(1, Ordering::Relaxed)
        );
    }
    o
}
unsafe extern "C" fn progress(u: *mut c_void, sent: u64, has: i32, total: u64) {
    let _ = std::panic::catch_unwind(|| {
        let c = &*u.cast::<messaging::WriterCallbacks>();
        emit(
            (&c.events as *const CallbackState).cast_mut().cast(),
            || Event::StreamProgress {
                stream_id: c.stream_id.clone(),
                bytes_sent: sent,
                total_size: (has != 0).then_some(total),
            },
        );
    });
}
unsafe extern "C" fn complete(u: *mut c_void, c: *const ffi::lk_data_stream_completion_t) {
    let _ = std::panic::catch_unwind(|| {
        let state = &*u.cast::<messaging::WriterCallbacks>();
        emit(
            (&state.events as *const CallbackState).cast_mut().cast(),
            || {
                let c = &*c;
                Event::StreamCompleted(StreamCompletion {
                    status: c.status,
                    stream_id: copy_string(c.stream_id),
                    bytes_sent: c.bytes_sent,
                    total_size: (c.has_total_size != 0).then_some(c.total_size),
                    reason: copy_string(c.reason),
                    error_domain: c.error_domain,
                    error_code: c.error_code,
                })
            },
        );
    });
}
impl Room {
    pub fn publish_data_with_options(&self, data: &[u8], o: &DataPublishOptions) -> Result<()> {
        let topic = cstring(&o.topic)?;
        let ids = o
            .destination_identities
            .iter()
            .map(|v| cstring(v))
            .collect::<Result<Vec<_>>>()?;
        let pointers: Vec<_> = ids.iter().map(|v| v.as_ptr()).collect();
        let mut n = native!(ffi::lk_data_publish_options_init);
        n.reliable = o.reliable.into();
        n.topic = topic.as_ptr();
        n.destination_identities = pointers.as_ptr();
        n.destination_identity_count = pointers.len();
        check(unsafe { ffi::lk_room_publish_data(self.as_raw(), data.as_ptr(), data.len(), &n) })
    }
    pub fn send_text_with_options(&self, value: &str, o: &DataStreamOptions) -> Result<()> {
        let value = cstring(value)?;
        let s = Strings::new(o)?;
        let mut n = native!(ffi::lk_text_send_options_init);
        common!(n, s, o);
        text!(n, s);
        check(unsafe { ffi::lk_room_send_text(self.as_raw(), value.as_ptr(), &n) })
    }
    pub fn send_bytes_with_options(&self, value: &[u8], o: &DataStreamOptions) -> Result<()> {
        let s = Strings::new(o)?;
        let mut n = native!(ffi::lk_byte_send_options_init);
        common!(n, s, o);
        binary!(n, s);
        check(unsafe { ffi::lk_room_send_bytes(self.as_raw(), value.as_ptr(), value.len(), &n) })
    }
    pub fn send_file_with_options(&self, path: &str, o: &DataStreamOptions) -> Result<()> {
        let path = cstring(path)?;
        let s = Strings::new(o)?;
        let mut n = native!(ffi::lk_file_send_options_init);
        common!(n, s, o);
        n.mime_type = s.mime.as_ptr();
        check(unsafe { ffi::lk_room_send_file(self.as_raw(), path.as_ptr(), &n) })
    }
    pub fn stream_text_with_options(&self, options: &DataStreamOptions) -> Result<TextWriter> {
        if options.update && options.stream_id.is_empty() {
            return Err(invalid("text stream updates require an existing stream_id"));
        }
        let o = stream_options(options);
        let s = Strings::new(&o)?;
        let mut n = native!(ffi::lk_stream_text_options_init);
        common!(n, s, o);
        text!(n, s);
        n.stream_id = s.stream.as_ptr();
        n.has_total_size = o.total_size.is_some().into();
        n.total_size = o.total_size.unwrap_or(0);
        n.update = o.update.into();
        n.version = o.version;
        let mut callbacks = Box::new(messaging::WriterCallbacks {
            events: (*self.callbacks).clone(),
            stream_id: o.stream_id.clone(),
        });
        let u = (&mut *callbacks as *mut messaging::WriterCallbacks).cast();
        n.on_progress = Some(progress);
        n.progress_user_data = u;
        n.on_complete = Some(complete);
        n.completion_user_data = u;
        let mut raw = ptr::null_mut();
        check(unsafe { ffi::lk_room_stream_text(self.as_raw(), &n, &mut raw) })?;
        Ok(TextWriter::from_ptr(raw, &self._sdk)?.retain_callbacks(callbacks))
    }
    pub fn stream_bytes_with_options(&self, options: &DataStreamOptions) -> Result<ByteWriter> {
        let o = stream_options(options);
        let s = Strings::new(&o)?;
        let mut n = native!(ffi::lk_stream_bytes_options_init);
        common!(n, s, o);
        binary!(n, s);
        n.stream_id = s.stream.as_ptr();
        n.has_total_size = o.total_size.is_some().into();
        n.total_size = o.total_size.unwrap_or(0);
        let mut callbacks = Box::new(messaging::WriterCallbacks {
            events: (*self.callbacks).clone(),
            stream_id: o.stream_id.clone(),
        });
        let u = (&mut *callbacks as *mut messaging::WriterCallbacks).cast();
        n.on_progress = Some(progress);
        n.progress_user_data = u;
        n.on_complete = Some(complete);
        n.completion_user_data = u;
        let mut raw = ptr::null_mut();
        check(unsafe { ffi::lk_room_stream_bytes(self.as_raw(), &n, &mut raw) })?;
        Ok(ByteWriter::from_ptr(raw, &self._sdk)?.retain_callbacks(callbacks))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn message_buffers_remain_valid_after_options_are_dropped() {
        let options = DataStreamOptions {
            topic: "topic".into(),
            destination_identities: vec!["peer".into()],
            attributes: vec![("key".into(), "value".into())],
            attached_stream_ids: vec!["attachment".into()],
            ..DataStreamOptions::default()
        };
        let strings = Strings::new(&options).unwrap();
        drop(options);
        unsafe {
            assert_eq!(copy_string(strings.destinations[0]), "peer");
            assert_eq!(copy_string(strings.attributes[0].value), "value");
            assert_eq!(copy_string(strings.attachments[0]), "attachment");
        }
        let options = DataStreamOptions {
            destination_identities: vec!["invalid\0peer".into()],
            ..DataStreamOptions::default()
        };
        assert!(Strings::new(&options).is_err());
        let options = TrackPublishOptions::default();
        options
            .with_native(|n| {
                assert_eq!(unsafe { copy_string(n.scalability_mode) }, "L3T3_KEY");
                assert_eq!(n.struct_size, std::mem::size_of_val(n));
                Ok(())
            })
            .unwrap();
    }
}

#[derive(Debug, Clone)]
pub struct TrackPublishOptions {
    pub source: ffi::lk_track_source_t,
    pub dtx: bool,
    pub red: bool,
    pub simulcast: bool,
    pub stream: String,
    pub video_codec: ffi::lk_video_codec_t,
    pub scalability_mode: String,
    pub backup_video_codec: Option<ffi::lk_video_codec_t>,
    pub backup_codec_policy: ffi::lk_backup_codec_policy_t,
    pub video_encoding: ffi::lk_video_encoding_t,
    pub backup_video_encoding: ffi::lk_video_encoding_t,
    pub user_timestamp: bool,
    pub frame_id: bool,
    pub user_data: bool,
    pub preconnect_buffer: bool,
    pub degradation_preference: Option<ffi::lk_video_degradation_preference_t>,
}
impl Default for TrackPublishOptions {
    fn default() -> Self {
        let o = native!(ffi::lk_track_publish_options_init);
        Self {
            source: o.source,
            dtx: o.dtx != 0,
            red: o.red != 0,
            simulcast: o.simulcast != 0,
            stream: String::new(),
            video_codec: o.video_codec,
            scalability_mode: unsafe { copy_string(o.scalability_mode) },
            backup_video_codec: (o.backup_video_codec_enabled != 0).then_some(o.backup_video_codec),
            backup_codec_policy: o.backup_codec_policy,
            video_encoding: o.video_encoding,
            backup_video_encoding: o.backup_video_encoding,
            user_timestamp: false,
            frame_id: false,
            user_data: false,
            preconnect_buffer: o.preconnect_buffer != 0,
            degradation_preference: (o.has_degradation_preference != 0)
                .then_some(o.degradation_preference),
        }
    }
}
impl TrackPublishOptions {
    pub(super) fn with_native<T>(
        &self,
        f: impl FnOnce(&ffi::lk_track_publish_options_t) -> Result<T>,
    ) -> Result<T> {
        let stream = cstring(&self.stream)?;
        let scalability = cstring(&self.scalability_mode)?;
        let mut n = native!(ffi::lk_track_publish_options_init);
        n.source = self.source;
        n.dtx = self.dtx.into();
        n.red = self.red.into();
        n.simulcast = self.simulcast.into();
        n.stream = stream.as_ptr();
        n.video_codec = self.video_codec;
        n.scalability_mode = scalability.as_ptr();
        n.backup_video_codec_enabled = self.backup_video_codec.is_some().into();
        n.backup_video_codec = self.backup_video_codec.unwrap_or(n.backup_video_codec);
        n.backup_codec_policy = self.backup_codec_policy;
        n.video_encoding = self.video_encoding;
        n.video_encoding.struct_size = std::mem::size_of_val(&n.video_encoding);
        n.backup_video_encoding = self.backup_video_encoding;
        n.backup_video_encoding.struct_size = std::mem::size_of_val(&n.backup_video_encoding);
        n.frame_metadata_features.user_timestamp = self.user_timestamp.into();
        n.frame_metadata_features.frame_id = self.frame_id.into();
        n.frame_metadata_features.user_data = self.user_data.into();
        n.preconnect_buffer = self.preconnect_buffer.into();
        n.has_degradation_preference = self.degradation_preference.is_some().into();
        n.degradation_preference = self
            .degradation_preference
            .unwrap_or(n.degradation_preference);
        f(&n)
    }
}
impl Room {
    pub fn publish_track_with_options(
        &self,
        track: &LocalTrack,
        options: &TrackPublishOptions,
    ) -> Result<()> {
        if !track.belongs_to(self) {
            return Err(invalid("track belongs to a different room"));
        }
        options.with_native(|o| {
            check(unsafe { ffi::lk_local_track_publish(self.as_raw(), track.as_raw(), o) })
        })
    }
    pub fn publish_screen_share_video(
        &self,
        track: &LocalTrack,
        options: &TrackPublishOptions,
    ) -> Result<()> {
        if !track.belongs_to(self) {
            return Err(invalid("track belongs to a different room"));
        }
        options.with_native(|o| {
            check(unsafe {
                ffi::lk_local_track_publish_screen_share_video(self.as_raw(), track.as_raw(), o)
            })
        })
    }
    pub fn publish_screen_share_audio(
        &self,
        track: &LocalTrack,
        options: &TrackPublishOptions,
    ) -> Result<()> {
        if !track.belongs_to(self) {
            return Err(invalid("track belongs to a different room"));
        }
        options.with_native(|o| {
            check(unsafe {
                ffi::lk_local_track_publish_screen_share_audio(self.as_raw(), track.as_raw(), o)
            })
        })
    }
    pub fn republish_all_tracks(&self) -> Result<()> {
        check(unsafe { ffi::lk_room_republish_all_tracks(self.as_raw()) })
    }
    pub fn publish_dtmf(&self, code: u32, digit: &str) -> Result<()> {
        let digit = cstring(digit)?;
        check(unsafe { ffi::lk_room_publish_dtmf(self.as_raw(), code, digit.as_ptr()) })
    }
}
impl LocalTrack {
    pub fn update_video_encoding(
        &self,
        encoding: &ffi::lk_video_encoding_t,
        backup: bool,
    ) -> Result<()> {
        let mut n = *encoding;
        n.struct_size = std::mem::size_of_val(&n);
        check(unsafe {
            ffi::lk_local_video_track_update_encoding(self.as_raw(), &n, backup.into())
        })
    }
    pub fn update_degradation_preference(
        &self,
        preference: ffi::lk_video_degradation_preference_t,
    ) -> Result<()> {
        check(unsafe {
            ffi::lk_local_video_track_update_degradation_preference(self.as_raw(), preference)
        })
    }
}

#[derive(Debug, Clone, Default)]
pub struct DataTrackPublishOptions {
    pub name: String,
    pub frame_encoding: Option<ffi::lk_data_track_frame_encoding_t>,
    pub custom_frame_encoding: String,
    pub schema: Option<SchemaId>,
}
#[derive(Debug, Clone)]
pub struct DataSubscriptionOptions {
    pub target_fps: Option<u32>,
    pub capacity: usize,
    pub max_partial_frames: usize,
}
impl Default for DataSubscriptionOptions {
    fn default() -> Self {
        let o = native!(ffi::lk_data_track_subscription_options_init);
        Self {
            target_fps: (o.has_target_fps != 0).then_some(o.target_fps),
            capacity: o.buffer_capacity,
            max_partial_frames: o.max_partial_frames,
        }
    }
}
impl DataSubscriptionOptions {
    fn native(&self) -> Result<ffi::lk_data_track_subscription_options_t> {
        if self.capacity == 0 {
            return Err(invalid("capacity must be positive"));
        }
        let mut n = native!(ffi::lk_data_track_subscription_options_init);
        n.has_target_fps = self.target_fps.is_some().into();
        n.target_fps = self.target_fps.unwrap_or(0);
        n.buffer_capacity = self.capacity;
        n.max_partial_frames = self.max_partial_frames;
        Ok(n)
    }
}
impl Room {
    pub fn publish_data_track_with_options(
        &mut self,
        options: &DataTrackPublishOptions,
    ) -> Result<DataTrack> {
        let name = cstring(&options.name)?;
        let custom = cstring(&options.custom_frame_encoding)?;
        let mut n = native!(ffi::lk_data_track_publish_options_init);
        n.name = name.as_ptr();
        n.has_frame_encoding = options.frame_encoding.is_some().into();
        n.frame_encoding = options.frame_encoding.unwrap_or(n.frame_encoding);
        n.custom_frame_encoding = custom.as_ptr();
        let mut publish = |id: Option<&ffi::lk_data_track_schema_id_t>| {
            if let Some(id) = id {
                n.has_schema = 1;
                n.schema = *id;
            }
            let mut raw = ptr::null_mut();
            messaging::check_data(unsafe {
                ffi::lk_room_publish_data_track(self.as_raw(), &n, &mut raw)
            })?;
            let track = DataTrack::from_ptr(raw, self)?;
            self.data_tracks.push(track.clone());
            Ok(track)
        };
        if let Some(id) = &options.schema {
            id.with_native(|id| publish(Some(id)))
        } else {
            publish(None)
        }
    }
    pub fn subscribe_data_track_with_options(
        &self,
        participant: &str,
        track: &str,
        options: &DataSubscriptionOptions,
    ) -> Result<DataReader> {
        let p = cstring(participant)?;
        let t = cstring(track)?;
        let n = options.native()?;
        let mut raw = ptr::null_mut();
        messaging::check_data(unsafe {
            ffi::lk_room_subscribe_data_track(self.as_raw(), p.as_ptr(), t.as_ptr(), &n, &mut raw)
        })?;
        DataReader::from_ptr(raw, &self._sdk)
    }
    pub fn update_data_subscription(
        &self,
        participant: &str,
        track: &str,
        options: &DataSubscriptionOptions,
    ) -> Result<()> {
        let p = cstring(participant)?;
        let t = cstring(track)?;
        let n = options.native()?;
        messaging::check_data(unsafe {
            ffi::lk_room_update_data_track_subscription_options(
                self.as_raw(),
                p.as_ptr(),
                t.as_ptr(),
                &n,
            )
        })
    }
}
