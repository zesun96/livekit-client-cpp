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
            let mut info = MaybeUninit::uninit();
            check(unsafe { ffi::lk_media_device_list_info(raw, i, info.as_mut_ptr()) })?;
            let info = unsafe { info.assume_init() };
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
            let mut info = MaybeUninit::uninit();
            check(unsafe { ffi::lk_screen_source_list_info(raw, i, info.as_mut_ptr()) })?;
            let info = unsafe { info.assume_init() };
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
