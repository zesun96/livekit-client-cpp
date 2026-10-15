use super::*;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TrackPublication {
    pub sid: String,
    pub name: String,
    pub kind: ffi::lk_track_kind_t,
    pub participant_identity: String,
    pub participant_sid: String,
}
pub(super) unsafe fn track_info(
    t: *const ffi::lk_track_publication_info_t,
    p: *const ffi::lk_participant_info_t,
) -> TrackPublication {
    TrackPublication {
        sid: copy_string((*t).sid),
        name: copy_string((*t).name),
        kind: (*t).kind,
        participant_identity: copy_string((*p).identity),
        participant_sid: copy_string((*p).sid),
    }
}
macro_rules! track_callback {
    ($name:ident,$variant:ident) => {
        pub(super) unsafe extern "C" fn $name(
            user: *mut c_void,
            _: *mut ffi::lk_room_t,
            t: *const ffi::lk_track_publication_info_t,
            p: *const ffi::lk_participant_info_t,
        ) {
            emit(user, || Event::$variant(track_info(t, p)));
        }
    };
}
track_callback!(on_track_published, TrackPublished);
track_callback!(on_track_subscribed, TrackSubscribed);
track_callback!(on_track_unsubscribed, TrackUnsubscribed);
pub(super) unsafe fn bytes(p: *const u8, n: usize) -> Vec<u8> {
    if n == 0 {
        Vec::new()
    } else {
        std::slice::from_raw_parts(p, n).to_vec()
    }
}
pub(super) unsafe extern "C" fn on_bytes(
    user: *mut c_void,
    _: *mut ffi::lk_room_t,
    e: *const ffi::lk_file_received_t,
) {
    emit(user, || {
        let e = &*e;
        Event::Bytes {
            data: bytes(e.data, e.data_size),
            topic: copy_string(e.topic),
            participant_identity: copy_string(e.participant_identity),
            stream_id: copy_string(e.stream_id),
            name: copy_string(e.name),
            mime_type: copy_string(e.mime_type),
        }
    });
    expanded_events::completed_byte_info(user, e);
}
pub(super) unsafe extern "C" fn on_audio(
    user: *mut c_void,
    _: *mut ffi::lk_room_t,
    t: *const ffi::lk_track_publication_info_t,
    p: *const ffi::lk_participant_info_t,
    f: *const ffi::lk_audio_frame_t,
) {
    emit(user, || {
        let f = &*f;
        Event::Audio {
            samples: if f.sample_count == 0 {
                Vec::new()
            } else {
                std::slice::from_raw_parts(f.data, f.sample_count).to_vec()
            },
            sample_rate: f.sample_rate,
            channels: f.num_channels,
            track_sid: copy_string((*t).sid),
            participant_identity: copy_string((*p).identity),
        }
    });
}
pub(super) unsafe extern "C" fn on_video(
    user: *mut c_void,
    _: *mut ffi::lk_room_t,
    t: *const ffi::lk_track_publication_info_t,
    p: *const ffi::lk_participant_info_t,
    f: *const ffi::lk_video_frame_t,
    m: *const ffi::lk_video_frame_metadata_t,
) {
    emit(user, || {
        let f = &*f;
        let metadata = if m.is_null() {
            VideoMetadata::default()
        } else {
            let m = &*m;
            VideoMetadata {
                user_timestamp_us: (m.has_user_timestamp_us != 0).then_some(m.user_timestamp_us),
                frame_id: (m.has_frame_id != 0).then_some(m.frame_id),
                user_data: (m.has_user_data != 0).then(|| bytes(m.user_data, m.user_data_size)),
            }
        };
        Event::Video {
            data: bytes(f.data, f.data_size),
            width: f.width,
            height: f.height,
            timestamp_us: f.timestamp_us,
            metadata,
            track_sid: copy_string((*t).sid),
            participant_identity: copy_string((*p).identity),
        }
    });
}
pub(super) unsafe extern "C" fn on_data_frame(
    user: *mut c_void,
    _: *mut ffi::lk_room_t,
    t: *const ffi::lk_data_track_info_t,
    p: *const ffi::lk_participant_info_t,
    f: *const ffi::lk_data_track_frame_view_t,
) {
    emit(user, || {
        let f = &*f;
        Event::DataFrame {
            data: bytes(f.data, f.data_size),
            timestamp: (f.has_user_timestamp != 0).then_some(f.user_timestamp),
            track_sid: copy_string((*t).sid),
            participant_identity: copy_string((*p).identity),
        }
    });
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StreamEvent {
    pub kind: ffi::lk_data_stream_event_type_t,
    pub stream_id: String,
    pub topic: String,
    pub participant_identity: String,
    pub content: Vec<u8>,
    pub chunk_index: u64,
    pub total_size: Option<u64>,
    pub reason: String,
}
unsafe extern "C" fn on_text_stream(
    user: *mut c_void,
    _: *mut ffi::lk_room_t,
    e: *const ffi::lk_text_stream_event_t,
) {
    emit(user, || {
        let e = &*e;
        Event::TextStream(StreamEvent {
            kind: e.type_,
            stream_id: copy_string(e.stream_id),
            topic: copy_string(e.topic),
            participant_identity: copy_string(e.participant_identity),
            content: bytes(e.content.cast(), e.content_size),
            chunk_index: e.chunk_index,
            total_size: (e.has_total_size != 0).then_some(e.total_size),
            reason: copy_string(e.reason),
        })
    });
    expanded_events::text_stream_info(user, e);
}
unsafe extern "C" fn on_byte_stream(
    user: *mut c_void,
    _: *mut ffi::lk_room_t,
    e: *const ffi::lk_byte_stream_event_t,
) {
    emit(user, || {
        let e = &*e;
        Event::ByteStream(StreamEvent {
            kind: e.type_,
            stream_id: copy_string(e.stream_id),
            topic: copy_string(e.topic),
            participant_identity: copy_string(e.participant_identity),
            content: bytes(e.content, e.content_size),
            chunk_index: e.chunk_index,
            total_size: (e.has_total_size != 0).then_some(e.total_size),
            reason: copy_string(e.reason),
        })
    });
    expanded_events::byte_stream_info(user, e);
}
impl Room {
    pub fn register_text_stream(&self, topic: &str) -> Result<()> {
        let topic = cstring(topic)?;
        check(unsafe {
            ffi::lk_room_register_text_stream_handler(
                self.as_raw(),
                topic.as_ptr(),
                Some(on_text_stream),
                (&*self.callbacks as *const CallbackState).cast_mut().cast(),
            )
        })
    }
    pub fn register_byte_stream(&self, topic: &str) -> Result<()> {
        let topic = cstring(topic)?;
        check(unsafe {
            ffi::lk_room_register_byte_stream_handler(
                self.as_raw(),
                topic.as_ptr(),
                Some(on_byte_stream),
                (&*self.callbacks as *const CallbackState).cast_mut().cast(),
            )
        })
    }
    pub fn unregister_text_stream(&self, topic: &str) -> Result<()> {
        let t = cstring(topic)?;
        check(unsafe { ffi::lk_room_unregister_text_stream_handler(self.as_raw(), t.as_ptr()) })
    }
    pub fn unregister_byte_stream(&self, topic: &str) -> Result<()> {
        let t = cstring(topic)?;
        check(unsafe { ffi::lk_room_unregister_byte_stream_handler(self.as_raw(), t.as_ptr()) })
    }
    pub fn remote_tracks(&self) -> Result<Vec<TrackPublication>> {
        unsafe {
            let mut raw = ptr::null_mut();
            check(ffi::lk_room_create_remote_participant_snapshot(
                self.as_raw(),
                &mut raw,
            ))?;
            let _snapshot = RemoteSnapshot(raw);
            let mut result = Vec::new();
            for i in 0..ffi::lk_remote_participant_list_count(raw) {
                let mut p = ptr::null();
                check(ffi::lk_remote_participant_list_at(raw, i, &mut p))?;
                let identity =
                    read_string(|b, n| ffi::lk_remote_participant_snapshot_identity(p, b, n))?;
                let sid = read_string(|b, n| ffi::lk_remote_participant_snapshot_sid(p, b, n))?;
                for j in 0..ffi::lk_remote_participant_snapshot_publication_count(p) {
                    let mut t = ptr::null();
                    check(ffi::lk_remote_participant_snapshot_publication_at(
                        p, j, &mut t,
                    ))?;
                    let mut info = native!(ffi::lk_remote_track_publication_snapshot_info_init);
                    check(ffi::lk_remote_track_publication_snapshot_info(t, &mut info))?;
                    result.push(TrackPublication {
                        sid: read_string(|b, n| {
                            ffi::lk_remote_track_publication_snapshot_sid(t, b, n)
                        })?,
                        name: read_string(|b, n| {
                            ffi::lk_remote_track_publication_snapshot_name(t, b, n)
                        })?,
                        kind: info.kind,
                        participant_identity: identity.clone(),
                        participant_sid: sid.clone(),
                    });
                }
            }
            Ok(result)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn video_callback_owns_frame_and_metadata() {
        let (sender, receiver) = mpsc::sync_channel(1);
        let state = CallbackState {
            sender,
            dropped: Arc::new(AtomicUsize::new(0)),
            notifier: Mutex::new(None),
        };
        let mut pixels = vec![128u8; 6];
        let mut extra = vec![1u8, 0, 255];
        let track = ffi::lk_track_publication_info_t {
            sid: c"track".as_ptr(),
            ..unsafe { std::mem::zeroed() }
        };
        let participant = ffi::lk_participant_info_t {
            identity: c"peer".as_ptr(),
            ..unsafe { std::mem::zeroed() }
        };
        let frame = ffi::lk_video_frame_t {
            data: pixels.as_ptr(),
            data_size: pixels.len(),
            width: 2,
            height: 2,
            timestamp_us: 42,
        };
        let mut metadata = native!(ffi::lk_video_frame_metadata_init);
        metadata.has_frame_id = 1;
        metadata.frame_id = 7;
        metadata.has_user_data = 1;
        metadata.user_data = extra.as_ptr();
        metadata.user_data_size = extra.len();
        unsafe {
            on_video(
                (&state as *const CallbackState).cast_mut().cast(),
                ptr::null_mut(),
                &track,
                &participant,
                &frame,
                &metadata,
            );
        }
        pixels.fill(0);
        extra.fill(0);
        drop(pixels);
        drop(extra);
        match receiver.try_recv().unwrap() {
            Event::Video {
                data,
                metadata,
                track_sid,
                participant_identity,
                ..
            } => {
                assert_eq!(data, [128; 6]);
                assert_eq!(metadata.frame_id, Some(7));
                assert_eq!(metadata.user_data, Some(vec![1, 0, 255]));
                assert_eq!(track_sid, "track");
                assert_eq!(participant_identity, "peer");
            }
            _ => panic!("unexpected callback event"),
        }
    }
}
