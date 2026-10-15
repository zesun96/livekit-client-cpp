use super::*;
owned_handle!(
    DeviceList,
    ffi::lk_media_device_list_t,
    ffi::lk_media_device_list_destroy
);
owned_handle!(
    ScreenList,
    ffi::lk_screen_source_list_t,
    ffi::lk_screen_source_list_destroy
);
#[derive(Debug, Clone)]
pub struct MediaDevice {
    pub id: String,
    pub label: String,
    pub kind: ffi::lk_media_device_kind_t,
    pub is_default: bool,
}
#[derive(Debug, Clone)]
pub struct ScreenSource {
    pub id: String,
    pub label: String,
    pub kind: ffi::lk_screen_source_kind_t,
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
}
impl Sdk {
    /// Enumerates devices without opening physical media devices.
    pub fn media_devices(&self) -> Result<Vec<MediaDevice>> {
        let mut raw = ptr::null_mut();
        check(unsafe { ffi::lk_media_device_list_create(&mut raw) })?;
        let _list = DeviceList::from_ptr(raw, self)?;
        let mut result = Vec::new();
        for i in 0..unsafe { ffi::lk_media_device_list_count(raw) } {
            let mut info = ffi::lk_media_device_info_t {
                struct_size: std::mem::size_of::<ffi::lk_media_device_info_t>(),
                kind: 0,
                is_default: 0,
            };
            check(unsafe { ffi::lk_media_device_list_info(raw, i, &mut info) })?;
            result.push(MediaDevice {
                id: read_string(|p, n| unsafe { ffi::lk_media_device_list_id(raw, i, p, n) })?,
                label: read_string(|p, n| unsafe {
                    ffi::lk_media_device_list_label(raw, i, p, n)
                })?,
                kind: info.kind,
                is_default: info.is_default != 0,
            });
        }
        Ok(result)
    }
    pub fn screen_sources(&self) -> Result<Vec<ScreenSource>> {
        let mut raw = ptr::null_mut();
        check(unsafe { ffi::lk_screen_source_list_create(&mut raw) })?;
        let _list = ScreenList::from_ptr(raw, self)?;
        let mut result = Vec::new();
        for i in 0..unsafe { ffi::lk_screen_source_list_count(raw) } {
            let mut info = ffi::lk_screen_source_info_t {
                struct_size: std::mem::size_of::<ffi::lk_screen_source_info_t>(),
                kind: 0,
                x: 0,
                y: 0,
                width: 0,
                height: 0,
            };
            check(unsafe { ffi::lk_screen_source_list_info(raw, i, &mut info) })?;
            result.push(ScreenSource {
                id: read_string(|p, n| unsafe { ffi::lk_screen_source_list_id(raw, i, p, n) })?,
                label: read_string(|p, n| unsafe {
                    ffi::lk_screen_source_list_label(raw, i, p, n)
                })?,
                kind: info.kind,
                x: info.x,
                y: info.y,
                width: info.width,
                height: info.height,
            });
        }
        Ok(result)
    }
}
impl Room {
    pub fn audio_output_device(&self) -> Result<String> {
        read_string(|p, n| unsafe { ffi::lk_room_audio_output_device(self.as_raw(), p, n) })
    }
    pub fn speaker_volume(&self) -> f32 {
        unsafe { ffi::lk_room_speaker_volume(self.as_raw()) }
    }
    pub fn speaker_is_muted(&self) -> bool {
        unsafe { ffi::lk_room_speaker_is_muted(self.as_raw()) != 0 }
    }
    pub fn is_recording(&self) -> bool {
        unsafe { ffi::lk_room_is_recording(self.as_raw()) != 0 }
    }
    pub fn set_audio_output_device(&self, id: &str) -> Result<()> {
        let id = cstring(id)?;
        check(unsafe { ffi::lk_room_set_audio_output_device(self.as_raw(), id.as_ptr()) })
    }
    pub fn set_speaker_volume(&self, volume: f32) -> Result<()> {
        check(unsafe { ffi::lk_room_set_speaker_volume(self.as_raw(), volume) })
    }
    pub fn set_speaker_muted(&self, muted: bool) -> Result<()> {
        check(unsafe { ffi::lk_room_set_speaker_muted(self.as_raw(), muted.into()) })
    }
    pub fn audio_playback_stats(&self) -> Result<ffi::lk_audio_playback_stats_t> {
        let mut stats = native!(ffi::lk_audio_playback_stats_init);
        check(unsafe { ffi::lk_room_audio_playback_stats(self.as_raw(), &mut stats) })?;
        Ok(stats)
    }
}

impl AudioSource {
    pub fn microphone_is_capturing(&self) -> bool {
        unsafe { ffi::lk_audio_source_microphone_is_capturing(self.as_raw()) != 0 }
    }
    pub fn microphone_device_id(&self) -> Result<String> {
        read_string(|p, n| unsafe {
            ffi::lk_audio_source_microphone_device_id(self.as_raw(), p, n)
        })
    }
    pub fn switch_microphone(&self, id: &str) -> Result<()> {
        let id = cstring(id)?;
        check(unsafe { ffi::lk_audio_source_microphone_switch_device(self.as_raw(), id.as_ptr()) })
    }
    pub fn microphone_is_muted(&self) -> bool {
        unsafe { ffi::lk_audio_source_microphone_is_muted(self.as_raw()) != 0 }
    }
    pub fn microphone_volume(&self) -> f32 {
        unsafe { ffi::lk_audio_source_microphone_volume(self.as_raw()) }
    }
    pub fn set_microphone_processing_options(&self, options: &AudioOptions) -> Result<()> {
        check(unsafe {
            ffi::lk_audio_source_microphone_set_processing_options(self.as_raw(), &options.native())
        })
    }
    pub fn microphone_processing_options(&self) -> Result<AudioOptions> {
        let mut o = native!(ffi::lk_audio_source_options_init);
        check(unsafe {
            ffi::lk_audio_source_microphone_processing_options(self.as_raw(), &mut o)
        })?;
        Ok(AudioOptions {
            sample_rate: o.sample_rate,
            channels: o.num_channels,
            queue_size_ms: o.queue_size_ms,
            echo_cancellation: o.echo_cancellation != 0,
            noise_suppression: o.noise_suppression != 0,
            auto_gain_control: o.auto_gain_control != 0,
        })
    }
    pub fn microphone_processing_stats(&self) -> Result<ffi::lk_microphone_processing_stats_t> {
        let mut o = native!(ffi::lk_microphone_processing_stats_init);
        check(unsafe { ffi::lk_audio_source_microphone_processing_stats(self.as_raw(), &mut o) })?;
        Ok(o)
    }
    pub fn system_audio_is_capturing(&self) -> bool {
        unsafe { ffi::lk_audio_source_system_audio_is_capturing(self.as_raw()) != 0 }
    }
    pub fn system_audio_device_id(&self) -> Result<String> {
        read_string(|p, n| unsafe {
            ffi::lk_audio_source_system_audio_device_id(self.as_raw(), p, n)
        })
    }
    pub fn switch_system_audio(&self, id: &str) -> Result<()> {
        let id = cstring(id)?;
        check(unsafe {
            ffi::lk_audio_source_system_audio_switch_device(self.as_raw(), id.as_ptr())
        })
    }
}
impl VideoSource {
    pub fn camera_is_capturing(&self) -> bool {
        unsafe { ffi::lk_video_source_camera_is_capturing(self.as_raw()) != 0 }
    }
    pub fn camera_device_id(&self) -> Result<String> {
        read_string(|p, n| unsafe { ffi::lk_video_source_camera_device_id(self.as_raw(), p, n) })
    }
    pub fn switch_camera(&self, id: &str) -> Result<()> {
        let id = cstring(id)?;
        check(unsafe { ffi::lk_video_source_camera_switch_device(self.as_raw(), id.as_ptr()) })
    }
    pub fn screen_is_capturing(&self) -> bool {
        unsafe { ffi::lk_video_source_screen_is_capturing(self.as_raw()) != 0 }
    }
    pub fn screen_source_id(&self) -> Result<String> {
        read_string(|p, n| unsafe { ffi::lk_video_source_screen_source_id(self.as_raw(), p, n) })
    }
    pub fn switch_screen(&self, id: &str) -> Result<()> {
        let id = cstring(id)?;
        check(unsafe { ffi::lk_video_source_screen_switch_source(self.as_raw(), id.as_ptr()) })
    }
}
