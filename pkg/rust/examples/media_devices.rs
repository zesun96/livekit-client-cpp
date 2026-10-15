use livekit_client_cpp::{ffi, Sdk};
use std::{mem::MaybeUninit, ptr};

struct Devices(*mut ffi::lk_media_device_list_t);
impl Drop for Devices {
    fn drop(&mut self) {
        unsafe { ffi::lk_media_device_list_destroy(self.0) }
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let _sdk = Sdk::new()?;
    // Enumeration only: this example does not open a microphone or speaker.
    unsafe {
        let mut raw = ptr::null_mut();
        if ffi::lk_media_device_list_create(&mut raw) != ffi::LK_STATUS_OK {
            return Err("device enumeration failed".into());
        }
        let _devices = Devices(raw);
        for index in 0..ffi::lk_media_device_list_count(raw) {
            let mut info = MaybeUninit::uninit();
            if ffi::lk_media_device_list_info(raw, index, info.as_mut_ptr()) != ffi::LK_STATUS_OK {
                return Err("device info failed".into());
            }
            let info = info.assume_init();
            let size = ffi::lk_media_device_list_label(raw, index, ptr::null_mut(), 0);
            let mut label = vec![0u8; size];
            ffi::lk_media_device_list_label(raw, index, label.as_mut_ptr().cast(), size);
            let end = label.iter().position(|&c| c == 0).unwrap_or(label.len());
            println!(
                "kind={} default={} {}",
                info.kind,
                info.is_default,
                String::from_utf8_lossy(&label[..end])
            );
        }
    }
    Ok(())
}
