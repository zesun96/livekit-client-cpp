use super::*;

owned_handle!(
    AudioSourceHandle,
    ffi::lk_audio_source_t,
    ffi::lk_audio_source_destroy
);
owned_handle!(
    VideoSourceHandle,
    ffi::lk_video_source_t,
    ffi::lk_video_source_destroy
);

/// Interleaved signed PCM input. Tracks retain the source until native track destruction.
#[derive(Clone)]
pub struct AudioSource {
    inner: Rc<AudioSourceHandle>,
    channels: u32,
}

#[derive(Debug, Clone)]
pub struct AudioOptions {
    pub sample_rate: u32,
    pub channels: u32,
    pub queue_size_ms: u32,
    pub echo_cancellation: bool,
    pub noise_suppression: bool,
    pub auto_gain_control: bool,
}
impl Default for AudioOptions {
    fn default() -> Self {
        let o = native!(ffi::lk_audio_source_options_init);
        Self {
            sample_rate: o.sample_rate,
            channels: o.num_channels,
            queue_size_ms: o.queue_size_ms,
            echo_cancellation: o.echo_cancellation != 0,
            noise_suppression: o.noise_suppression != 0,
            auto_gain_control: o.auto_gain_control != 0,
        }
    }
}
impl AudioOptions {
    pub(super) fn native(&self) -> ffi::lk_audio_source_options_t {
        let mut o = native!(ffi::lk_audio_source_options_init);
        o.sample_rate = self.sample_rate;
        o.num_channels = self.channels;
        o.queue_size_ms = self.queue_size_ms;
        o.echo_cancellation = self.echo_cancellation.into();
        o.noise_suppression = self.noise_suppression.into();
        o.auto_gain_control = self.auto_gain_control.into();
        o
    }
}
impl Sdk {
    pub fn audio_source(&self, options: &AudioOptions) -> Result<AudioSource> {
        let mut raw = ptr::null_mut();
        check(unsafe { ffi::lk_audio_source_create(&options.native(), &mut raw) })?;
        Ok(AudioSource {
            inner: Rc::new(AudioSourceHandle::from_ptr(raw, self)?),
            channels: options.channels,
        })
    }
    /// Opens a physical microphone. Do not call from offline tests.
    pub fn microphone(&self, device_id: &str, options: &AudioOptions) -> Result<AudioSource> {
        let id = cstring(device_id)?;
        let mut o = native!(ffi::lk_microphone_capture_options_init);
        o.device_id = id.as_ptr();
        o.queue_size_ms = options.queue_size_ms;
        o.echo_cancellation = options.echo_cancellation.into();
        o.noise_suppression = options.noise_suppression.into();
        o.auto_gain_control = options.auto_gain_control.into();
        let mut raw = ptr::null_mut();
        check(unsafe { ffi::lk_audio_source_create_microphone(&o, &mut raw) })?;
        Ok(AudioSource {
            inner: Rc::new(AudioSourceHandle::from_ptr(raw, self)?),
            channels: 1,
        })
    }
    /// Opens physical system-audio capture (48 kHz stereo).
    pub fn system_audio(&self, device_id: &str) -> Result<AudioSource> {
        let id = cstring(device_id)?;
        let mut o = native!(ffi::lk_system_audio_capture_options_init);
        o.device_id = id.as_ptr();
        let mut raw = ptr::null_mut();
        check(unsafe { ffi::lk_audio_source_create_system_audio(&o, &mut raw) })?;
        Ok(AudioSource {
            inner: Rc::new(AudioSourceHandle::from_ptr(raw, self)?),
            channels: 2,
        })
    }
    pub fn video_source(&self, screencast: bool) -> Result<VideoSource> {
        let mut o = native!(ffi::lk_video_source_options_init);
        o.is_screencast = screencast.into();
        let mut raw = ptr::null_mut();
        check(unsafe { ffi::lk_video_source_create(&o, &mut raw) })?;
        Ok(VideoSource(Rc::new(VideoSourceHandle::from_ptr(
            raw, self,
        )?)))
    }
    /// Opens a physical camera. Empty device_id selects the native default.
    pub fn camera(
        &self,
        device_id: &str,
        width: u32,
        height: u32,
        fps: u32,
    ) -> Result<VideoSource> {
        let id = cstring(device_id)?;
        let mut o = native!(ffi::lk_camera_capture_options_init);
        o.device_id = id.as_ptr();
        o.width = width;
        o.height = height;
        o.frames_per_second = fps;
        let mut raw = ptr::null_mut();
        check(unsafe { ffi::lk_video_source_create_camera(&o, &mut raw) })?;
        Ok(VideoSource(Rc::new(VideoSourceHandle::from_ptr(
            raw, self,
        )?)))
    }
    pub fn screen_capture(
        &self,
        source_id: &str,
        fps: u32,
        include_cursor: bool,
    ) -> Result<VideoSource> {
        let id = cstring(source_id)?;
        let mut o = native!(ffi::lk_screen_capture_options_init);
        o.source_id = id.as_ptr();
        o.frames_per_second = fps;
        o.include_cursor = include_cursor.into();
        let mut raw = ptr::null_mut();
        check(unsafe { ffi::lk_video_source_create_screen(&o, &mut raw) })?;
        Ok(VideoSource(Rc::new(VideoSourceHandle::from_ptr(
            raw, self,
        )?)))
    }
}
impl AudioSource {
    pub fn as_raw(&self) -> *mut ffi::lk_audio_source_t {
        self.inner.as_raw()
    }
    pub fn capture(&self, samples: &[i16]) -> Result<()> {
        if self.channels == 0 || samples.is_empty() || samples.len() % self.channels as usize != 0 {
            return Err(invalid("PCM must contain complete interleaved samples"));
        }
        let count = u32::try_from(samples.len() / self.channels as usize)
            .map_err(|_| invalid("audio frame too large"))?;
        check(unsafe { ffi::lk_audio_source_capture_frame(self.as_raw(), samples.as_ptr(), count) })
    }
    pub fn clear_queue(&self) -> Result<()> {
        check(unsafe { ffi::lk_audio_source_clear_queue(self.as_raw()) })
    }
    pub fn queued_duration_ms(&self) -> Result<u32> {
        let mut ms = 0;
        check(unsafe { ffi::lk_audio_source_queued_duration_ms(self.as_raw(), &mut ms) })?;
        Ok(ms)
    }
    pub fn wait_for_playout(&self, timeout_ms: u32) -> Result<()> {
        check(unsafe { ffi::lk_audio_source_wait_for_playout(self.as_raw(), timeout_ms) })
    }
    pub fn start_microphone(&self) -> Result<()> {
        check(unsafe { ffi::lk_audio_source_microphone_start(self.as_raw()) })
    }
    pub fn stop_microphone(&self) -> Result<()> {
        check(unsafe { ffi::lk_audio_source_microphone_stop(self.as_raw()) })
    }
    pub fn start_system_audio(&self) -> Result<()> {
        check(unsafe { ffi::lk_audio_source_system_audio_start(self.as_raw()) })
    }
    pub fn stop_system_audio(&self) -> Result<()> {
        check(unsafe { ffi::lk_audio_source_system_audio_stop(self.as_raw()) })
    }
    pub fn set_microphone_muted(&self, muted: bool) -> Result<()> {
        check(unsafe { ffi::lk_audio_source_microphone_set_muted(self.as_raw(), muted.into()) })
    }
    pub fn set_microphone_volume(&self, volume: f32) -> Result<()> {
        check(unsafe { ffi::lk_audio_source_microphone_set_volume(self.as_raw(), volume) })
    }
}

#[derive(Clone)]
pub struct VideoSource(Rc<VideoSourceHandle>);

/// Borrowed input buffers are copied by the native SDK during capture.
pub struct VideoInput<'a> {
    pub data: &'a [u8],
    pub width: u32,
    pub height: u32,
    pub timestamp_us: i64,
    pub rotation: ffi::lk_video_rotation_t,
    pub format: ffi::lk_video_buffer_type_t,
    pub planes: &'a [ffi::lk_video_plane_t],
    pub metadata: Option<&'a VideoMetadata>,
}
impl VideoSource {
    pub fn capture_frame(&self, input: &VideoInput<'_>) -> Result<()> {
        let metadata = input.metadata.map(|m| {
            let mut o = native!(ffi::lk_video_frame_metadata_init);
            o.has_user_timestamp_us = m.user_timestamp_us.is_some().into();
            o.user_timestamp_us = m.user_timestamp_us.unwrap_or(0);
            o.has_frame_id = m.frame_id.is_some().into();
            o.frame_id = m.frame_id.unwrap_or(0);
            if let Some(data) = &m.user_data {
                o.has_user_data = 1;
                o.user_data = data.as_ptr();
                o.user_data_size = data.len();
            }
            o
        });
        let mut o = native!(ffi::lk_video_frame_input_init);
        o.data = input.data.as_ptr();
        o.data_size = input.data.len();
        o.width = input.width;
        o.height = input.height;
        o.timestamp_us = input.timestamp_us;
        o.rotation = input.rotation;
        o.format = input.format;
        o.planes = input.planes.as_ptr();
        o.plane_count = input.planes.len();
        o.metadata = metadata.as_ref().map_or(ptr::null(), |m| m);
        check(unsafe { ffi::lk_video_source_capture_frame(self.as_raw(), &o) })
    }
    pub fn as_raw(&self) -> *mut ffi::lk_video_source_t {
        self.0.as_raw()
    }
    pub fn capture_i420(
        &self,
        bytes: &[u8],
        width: u32,
        height: u32,
        timestamp_us: i64,
    ) -> Result<()> {
        check(unsafe {
            ffi::lk_video_source_capture_i420(
                self.as_raw(),
                bytes.as_ptr(),
                bytes.len(),
                width,
                height,
                timestamp_us,
            )
        })
    }
    pub fn dimensions(&self) -> Result<(u32, u32)> {
        let (mut width, mut height) = (0, 0);
        check(unsafe { ffi::lk_video_source_dimensions(self.as_raw(), &mut width, &mut height) })?;
        Ok((width, height))
    }
    pub fn start_camera(&self) -> Result<()> {
        check(unsafe { ffi::lk_video_source_camera_start(self.as_raw()) })
    }
    pub fn stop_camera(&self) -> Result<()> {
        check(unsafe { ffi::lk_video_source_camera_stop(self.as_raw()) })
    }
    pub fn start_screen(&self) -> Result<()> {
        check(unsafe { ffi::lk_video_source_screen_start(self.as_raw()) })
    }
    pub fn stop_screen(&self) -> Result<()> {
        check(unsafe { ffi::lk_video_source_screen_stop(self.as_raw()) })
    }
}

enum TrackSource {
    Audio(AudioSource),
    Video(VideoSource),
}
struct TrackInner {
    handle: NonNull<ffi::lk_local_track_t>,
    source: TrackSource,
    room_identity: Rc<()>,
    _room: Rc<RoomLifetime>,
}
impl Drop for TrackInner {
    fn drop(&mut self) {
        unsafe {
            ffi::lk_local_track_destroy(self.handle.as_ptr());
        }
    }
}

/// The creating room retains every track through native room destruction, even
/// if the caller drops its clone. Each track in turn retains its media source.
#[derive(Clone)]
pub struct LocalTrack(Rc<TrackInner>);
impl LocalTrack {
    pub(super) fn belongs_to(&self, room: &Room) -> bool {
        Rc::ptr_eq(&self.0.room_identity, &room.identity)
    }
    pub(super) fn sdk(&self) -> &Sdk {
        match &self.0.source {
            TrackSource::Audio(source) => &source.inner._sdk,
            TrackSource::Video(source) => &source.0._sdk,
        }
    }
    pub fn as_raw(&self) -> *mut ffi::lk_local_track_t {
        self.0.handle.as_ptr()
    }
    pub fn set_muted(&self, muted: bool) -> Result<()> {
        check(unsafe { ffi::lk_local_track_set_muted(self.as_raw(), muted.into()) })
    }
    pub fn unpublish(&self, stop: bool) -> Result<()> {
        check(unsafe { ffi::lk_local_track_unpublish(self.as_raw(), stop.into()) })
    }
    pub fn rtc_stats(&self) -> Result<String> {
        read_string(|p, n| unsafe { ffi::lk_local_track_rtc_stats(self.as_raw(), p, n) })
    }
    /// The retained source can be used to push additional frames.
    pub fn audio_source(&self) -> Option<&AudioSource> {
        if let TrackSource::Audio(s) = &self.0.source {
            Some(s)
        } else {
            None
        }
    }
    pub fn video_source(&self) -> Option<&VideoSource> {
        if let TrackSource::Video(s) = &self.0.source {
            Some(s)
        } else {
            None
        }
    }
}
impl Room {
    pub fn create_audio_track(&mut self, name: &str, source: &AudioSource) -> Result<LocalTrack> {
        let name = cstring(name)?;
        let mut raw = ptr::null_mut();
        check(unsafe {
            ffi::lk_room_create_audio_track(self.as_raw(), name.as_ptr(), source.as_raw(), &mut raw)
        })?;
        self.retain_track(raw, TrackSource::Audio(source.clone()))
    }
    pub fn create_video_track(&mut self, name: &str, source: &VideoSource) -> Result<LocalTrack> {
        let name = cstring(name)?;
        let mut raw = ptr::null_mut();
        check(unsafe {
            ffi::lk_room_create_video_track(self.as_raw(), name.as_ptr(), source.as_raw(), &mut raw)
        })?;
        self.retain_track(raw, TrackSource::Video(source.clone()))
    }
    fn retain_track(
        &mut self,
        raw: *mut ffi::lk_local_track_t,
        source: TrackSource,
    ) -> Result<LocalTrack> {
        let track = LocalTrack(Rc::new(TrackInner {
            handle: NonNull::new(raw).ok_or_else(|| invalid("null track"))?,
            source,
            room_identity: self.identity.clone(),
            _room: self.lifetime.clone(),
        }));
        self.tracks.push(track.clone());
        Ok(track)
    }
    pub fn publish_track(
        &mut self,
        track: &LocalTrack,
        source: ffi::lk_track_source_t,
    ) -> Result<()> {
        if !Rc::ptr_eq(&self.identity, &track.0.room_identity) {
            return Err(invalid("track belongs to a different room"));
        }
        let mut o = native!(ffi::lk_track_publish_options_init);
        o.source = source;
        check(unsafe { ffi::lk_local_track_publish(self.as_raw(), track.as_raw(), &o) })
    }
}

/// Pull readers distinguish temporary absence of frames from terminal closure.
#[derive(Debug, PartialEq, Eq)]
pub enum Read<T> {
    Frame(T),
    Empty,
    Closed,
}

owned_handle!(
    AudioStream,
    ffi::lk_audio_stream_t,
    ffi::lk_audio_stream_destroy
);
owned_handle!(
    VideoStream,
    ffi::lk_video_stream_t,
    ffi::lk_video_stream_destroy
);
owned_handle!(
    AudioFrame,
    ffi::lk_owned_audio_frame_t,
    ffi::lk_owned_audio_frame_destroy
);
owned_handle!(
    VideoFrame,
    ffi::lk_owned_video_frame_t,
    ffi::lk_owned_video_frame_destroy
);
owned_handle!(
    Recorder,
    ffi::lk_track_recorder_t,
    ffi::lk_track_recorder_destroy
);

macro_rules! reader {
    ($stream:ident, $frame:ident, $read:path, $close:path, $closed:path, $dropped:path) => {
        impl $stream {
            pub fn read_for(&mut self, timeout_ms: u32) -> Result<Read<$frame>> {
                let mut raw = ptr::null_mut();
                match unsafe { $read(self.as_raw(), timeout_ms, &mut raw) } {
                    ffi::LK_MEDIA_STREAM_READ_FRAME => {
                        Ok(Read::Frame($frame::from_ptr(raw, &self._sdk)?))
                    }
                    ffi::LK_MEDIA_STREAM_READ_EMPTY => Ok(Read::Empty),
                    ffi::LK_MEDIA_STREAM_READ_CLOSED => Ok(Read::Closed),
                    _ => {
                        check(ffi::LK_STATUS_OPERATION_FAILED)?;
                        unreachable!()
                    }
                }
            }
            pub fn close(&mut self) {
                unsafe {
                    $close(self.as_raw());
                }
            }
            pub fn is_closed(&self) -> bool {
                unsafe { $closed(self.as_raw()) != 0 }
            }
            pub fn dropped_frames(&self) -> usize {
                unsafe { $dropped(self.as_raw()) }
            }
        }
    };
}
reader!(
    AudioStream,
    AudioFrame,
    ffi::lk_audio_stream_read_for,
    ffi::lk_audio_stream_close,
    ffi::lk_audio_stream_is_closed,
    ffi::lk_audio_stream_dropped_frames
);
reader!(
    VideoStream,
    VideoFrame,
    ffi::lk_video_stream_read_for,
    ffi::lk_video_stream_close,
    ffi::lk_video_stream_is_closed,
    ffi::lk_video_stream_dropped_frames
);

impl AudioFrame {
    fn data(&self) -> Result<ffi::lk_audio_frame_t> {
        let mut data = MaybeUninit::uninit();
        check(unsafe { ffi::lk_owned_audio_frame_data(self.as_raw(), data.as_mut_ptr()) })?;
        Ok(unsafe { data.assume_init() })
    }
    pub fn samples(&self) -> Result<&[i16]> {
        let data = self.data()?;
        let len = data.sample_count;
        if len == 0 {
            return Ok(&[]);
        }
        Ok(unsafe { std::slice::from_raw_parts(data.data, len) })
    }
    pub fn sample_rate(&self) -> Result<u32> {
        Ok(self.data()?.sample_rate)
    }
    pub fn channels(&self) -> Result<u32> {
        Ok(self.data()?.num_channels)
    }
}
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct VideoMetadata {
    pub user_timestamp_us: Option<u64>,
    pub frame_id: Option<u32>,
    pub user_data: Option<Vec<u8>>,
}
impl VideoFrame {
    fn data(&self) -> Result<ffi::lk_video_frame_t> {
        let mut data = MaybeUninit::uninit();
        check(unsafe { ffi::lk_owned_video_frame_data(self.as_raw(), data.as_mut_ptr()) })?;
        Ok(unsafe { data.assume_init() })
    }
    pub fn bytes(&self) -> Result<&[u8]> {
        let d = self.data()?;
        if d.data_size == 0 {
            return Ok(&[]);
        }
        Ok(unsafe { std::slice::from_raw_parts(d.data, d.data_size) })
    }
    pub fn dimensions(&self) -> Result<(u32, u32)> {
        let d = self.data()?;
        Ok((d.width, d.height))
    }
    pub fn timestamp_us(&self) -> Result<i64> {
        Ok(self.data()?.timestamp_us)
    }
    pub fn metadata(&self) -> Result<VideoMetadata> {
        let mut d = native!(ffi::lk_video_frame_metadata_init);
        check(unsafe { ffi::lk_owned_video_frame_metadata(self.as_raw(), &mut d) })?;
        Ok(VideoMetadata {
            user_timestamp_us: (d.has_user_timestamp_us != 0).then_some(d.user_timestamp_us),
            frame_id: (d.has_frame_id != 0).then_some(d.frame_id),
            user_data: if d.has_user_data == 0 {
                None
            } else if d.user_data_size == 0 {
                Some(Vec::new())
            } else {
                Some(unsafe { std::slice::from_raw_parts(d.user_data, d.user_data_size) }.to_vec())
            },
        })
    }
}
impl Room {
    pub fn audio_stream(
        &self,
        participant: &str,
        track: &str,
        capacity: usize,
    ) -> Result<AudioStream> {
        if capacity == 0 {
            return Err(invalid("capacity must be positive"));
        }
        let p = cstring(participant)?;
        let t = cstring(track)?;
        let mut o = native!(ffi::lk_media_stream_options_init);
        o.capacity = capacity;
        let mut raw = ptr::null_mut();
        check(unsafe {
            ffi::lk_room_create_audio_stream(self.as_raw(), p.as_ptr(), t.as_ptr(), &o, &mut raw)
        })?;
        AudioStream::from_ptr(raw, &self._sdk)
    }
    pub fn video_stream(
        &self,
        participant: &str,
        track: &str,
        capacity: usize,
    ) -> Result<VideoStream> {
        if capacity == 0 {
            return Err(invalid("capacity must be positive"));
        }
        let p = cstring(participant)?;
        let t = cstring(track)?;
        let mut o = native!(ffi::lk_media_stream_options_init);
        o.capacity = capacity;
        let mut raw = ptr::null_mut();
        check(unsafe {
            ffi::lk_room_create_video_stream(self.as_raw(), p.as_ptr(), t.as_ptr(), &o, &mut raw)
        })?;
        VideoStream::from_ptr(raw, &self._sdk)
    }
    pub fn start_recording(
        &self,
        participant: &str,
        track: &str,
        path: &str,
        capacity: usize,
    ) -> Result<Recorder> {
        if capacity == 0 {
            return Err(invalid("capacity must be positive"));
        }
        let p = cstring(participant)?;
        let t = cstring(track)?;
        let path = cstring(path)?;
        let mut o = native!(ffi::lk_track_recorder_options_init);
        o.output_path = path.as_ptr();
        o.queue_capacity = capacity;
        let mut raw = ptr::null_mut();
        check(unsafe {
            ffi::lk_room_start_track_recording(self.as_raw(), p.as_ptr(), t.as_ptr(), &o, &mut raw)
        })?;
        Recorder::from_ptr(raw, &self._sdk)
    }
}
impl Recorder {
    pub fn stop(&mut self) {
        unsafe {
            ffi::lk_track_recorder_stop(self.as_raw());
        }
    }
    pub fn output_path(&self) -> Result<String> {
        read_string(|p, n| unsafe { ffi::lk_track_recorder_output_path(self.as_raw(), p, n) })
    }
    pub fn error(&self) -> Result<String> {
        read_string(|p, n| unsafe { ffi::lk_track_recorder_error(self.as_raw(), p, n) })
    }
    pub fn stats(&self) -> Result<ffi::lk_track_recorder_stats_t> {
        let mut stats = native!(ffi::lk_track_recorder_stats_init);
        check(unsafe { ffi::lk_track_recorder_get_stats(self.as_raw(), &mut stats) })?;
        Ok(stats)
    }
}
