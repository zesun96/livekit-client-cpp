use super::*;
use std::cell::RefCell;

#[derive(Clone)]
pub struct IceServer {
    pub urls: Vec<String>,
    pub username: String,
    pub password: String,
}
#[derive(Clone)]
pub struct E2eeOptions {
    pub shared_key: Vec<u8>,
    pub key_derivation: ffi::lk_e2ee_key_derivation_t,
}

#[derive(Debug, Clone)]
pub struct RemoteTrackSettings {
    pub enabled: bool,
    pub quality: Option<ffi::lk_video_quality_t>,
    pub width: u32,
    pub height: u32,
    pub fps: u32,
}
impl Default for RemoteTrackSettings {
    fn default() -> Self {
        Self {
            enabled: true,
            quality: None,
            width: 0,
            height: 0,
            fps: 0,
        }
    }
}
impl ConnectOptions {
    pub(super) fn with_native<T>(
        &self,
        f: impl FnOnce(&ffi::lk_room_connect_options_t) -> Result<T>,
    ) -> Result<T> {
        let urls = self
            .ice_servers
            .iter()
            .map(|s| {
                s.urls
                    .iter()
                    .map(|u| cstring(u))
                    .collect::<Result<Vec<_>>>()
            })
            .collect::<Result<Vec<_>>>()?;
        let url_pointers: Vec<Vec<_>> = urls
            .iter()
            .map(|u| u.iter().map(|s| s.as_ptr()).collect())
            .collect();
        let credentials = self
            .ice_servers
            .iter()
            .map(|s| Ok((cstring(&s.username)?, cstring(&s.password)?)))
            .collect::<Result<Vec<_>>>()?;
        let servers: Vec<_> = self
            .ice_servers
            .iter()
            .enumerate()
            .map(|(i, _)| {
                let mut s = native!(ffi::lk_ice_server_init);
                s.urls = url_pointers[i].as_ptr();
                s.url_count = url_pointers[i].len();
                s.username = credentials[i].0.as_ptr();
                s.password = credentials[i].1.as_ptr();
                s
            })
            .collect();
        let e2ee = self.e2ee.as_ref().map(|e| {
            let mut o = native!(ffi::lk_e2ee_options_init);
            o.enabled = 1;
            o.shared_key = e.shared_key.as_ptr();
            o.shared_key_size = e.shared_key.len();
            o.key_derivation = e.key_derivation;
            o
        });
        let mut o = native!(ffi::lk_room_connect_options_init);
        o.auto_subscribe = self.auto_subscribe.into();
        o.adaptive_stream = self.adaptive_stream.into();
        o.dynacast = self.dynacast.into();
        o.join_retries = self.join_retries;
        o.reconnect_timeout_ms = self.reconnect_timeout_ms;
        o.ice_servers = servers.as_ptr();
        o.ice_server_count = servers.len();
        o.ice_transport_type = self.ice_transport_type;
        o.continual_gathering_policy = self.continual_gathering_policy;
        o.e2ee_options = e2ee.as_ref().map_or(ptr::null(), |e| e);
        f(&o)
    }
}

pub struct TokenResponse {
    pub server_url: String,
    pub participant_token: String,
}
pub(super) struct TokenSourceState {
    fetch: Box<dyn Fn(bool) -> Result<TokenResponse> + Send + Sync>,
}
struct TokenStorage {
    url: CString,
    token: CString,
}
thread_local! { static TOKEN_RESPONSE:RefCell<Option<TokenStorage>> = const { RefCell::new(None) }; }
unsafe extern "C" fn on_token(
    user: *mut c_void,
    _: *const ffi::lk_token_source_fetch_options_t,
    force: i32,
    response: *mut ffi::lk_token_source_response_t,
) -> ffi::lk_status_t {
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| -> Result<()> {
        let state = &*user.cast::<TokenSourceState>();
        let value = (state.fetch)(force != 0)?;
        let value = TokenStorage {
            url: cstring(&value.server_url)?,
            token: cstring(&value.participant_token)?,
        };
        TOKEN_RESPONSE.with(|slot| {
            (*response).server_url = value.url.as_ptr();
            (*response).participant_token = value.token.as_ptr();
            *slot.borrow_mut() = Some(value);
        });
        Ok(())
    }));
    if matches!(result, Ok(Ok(()))) {
        ffi::LK_STATUS_OK
    } else {
        ffi::LK_STATUS_OPERATION_FAILED
    }
}
impl Room {
    /// Called initially and during token refresh on SDK threads. The bool asks
    /// the provider to bypass its token cache. Panics are contained at the ABI.
    pub fn connect_with_token_source(
        &mut self,
        options: &ConnectOptions,
        fetch: impl Fn(bool) -> Result<TokenResponse> + Send + Sync + 'static,
    ) -> Result<()> {
        let mut state = Box::new(TokenSourceState {
            fetch: Box::new(fetch),
        });
        let user = (&mut *state as *mut TokenSourceState).cast();
        // Retain even after a failed connect: recovery callbacks must never see
        // a freed context. All contexts are released after room destruction.
        self.token_sources.push(state);
        let fetch_options = native!(ffi::lk_token_source_fetch_options_init);
        options.with_native(|options| {
            check(unsafe {
                ffi::lk_room_connect_with_token_source_and_options(
                    self.as_raw(),
                    Some(on_token),
                    user,
                    &fetch_options,
                    options,
                )
            })
        })
    }
    pub fn e2ee_is_configured(&self) -> bool {
        unsafe { ffi::lk_room_e2ee_is_configured(self.as_raw()) != 0 }
    }
    pub fn e2ee_is_enabled(&self) -> bool {
        unsafe { ffi::lk_room_e2ee_is_enabled(self.as_raw()) != 0 }
    }
    pub fn e2ee_set_enabled(&self, enabled: bool) -> Result<()> {
        check(unsafe { ffi::lk_room_e2ee_set_enabled(self.as_raw(), enabled.into()) })
    }
    pub fn e2ee_set_shared_key(&self, key: &[u8], index: usize) -> Result<()> {
        check(unsafe {
            ffi::lk_room_e2ee_set_shared_key(self.as_raw(), key.as_ptr(), key.len(), index)
        })
    }
    pub fn e2ee_export_shared_key(&self, index: usize) -> Result<Vec<u8>> {
        messaging::read_bytes(|p, n| unsafe {
            ffi::lk_room_e2ee_export_shared_key(self.as_raw(), index, p, n)
        })
    }
    pub fn e2ee_ratchet_shared_key(&self, index: usize) -> Result<()> {
        check(unsafe { ffi::lk_room_e2ee_ratchet_shared_key(self.as_raw(), index) })
    }
    pub fn e2ee_remove_shared_key(&self, index: usize) -> Result<()> {
        check(unsafe { ffi::lk_room_e2ee_remove_shared_key(self.as_raw(), index) })
    }
    pub fn e2ee_set_participant_key(
        &self,
        participant: &str,
        key: &[u8],
        index: usize,
    ) -> Result<()> {
        let p = cstring(participant)?;
        check(unsafe {
            ffi::lk_room_e2ee_set_participant_key(
                self.as_raw(),
                p.as_ptr(),
                key.as_ptr(),
                key.len(),
                index,
            )
        })
    }
    pub fn e2ee_export_participant_key(&self, participant: &str, index: usize) -> Result<Vec<u8>> {
        let p = cstring(participant)?;
        messaging::read_bytes(|b, n| unsafe {
            ffi::lk_room_e2ee_export_participant_key(self.as_raw(), p.as_ptr(), index, b, n)
        })
    }
    pub fn e2ee_clear_keys(&self) -> Result<()> {
        check(unsafe { ffi::lk_room_e2ee_clear_keys(self.as_raw()) })
    }
    pub fn e2ee_ratchet_participant_key(&self, participant: &str, index: usize) -> Result<()> {
        let p = cstring(participant)?;
        check(unsafe {
            ffi::lk_room_e2ee_ratchet_participant_key(self.as_raw(), p.as_ptr(), index)
        })
    }
    pub fn e2ee_remove_participant_key(&self, participant: &str, index: usize) -> Result<()> {
        let p = cstring(participant)?;
        check(unsafe { ffi::lk_room_e2ee_remove_participant_key(self.as_raw(), p.as_ptr(), index) })
    }
    pub fn e2ee_remove_participant_keys(&self, participant: &str) -> Result<()> {
        let p = cstring(participant)?;
        check(unsafe { ffi::lk_room_e2ee_remove_participant_keys(self.as_raw(), p.as_ptr()) })
    }
    pub fn e2ee_data_key_index(&self) -> usize {
        unsafe { ffi::lk_room_e2ee_data_key_index(self.as_raw()) }
    }
    pub fn e2ee_set_frame_cryptor_key_index(
        &self,
        track: &str,
        direction: ffi::lk_frame_cryptor_direction_t,
        index: usize,
    ) -> Result<()> {
        let t = cstring(track)?;
        check(unsafe {
            ffi::lk_room_e2ee_set_frame_cryptor_key_index(
                self.as_raw(),
                t.as_ptr(),
                direction,
                index,
            )
        })
    }
    pub fn e2ee_set_participant_enabled(&self, participant: &str, enabled: bool) -> Result<usize> {
        let p = cstring(participant)?;
        let mut count = 0;
        check(unsafe {
            ffi::lk_room_e2ee_set_participant_enabled(
                self.as_raw(),
                p.as_ptr(),
                enabled.into(),
                &mut count,
            )
        })?;
        Ok(count)
    }
    pub fn e2ee_frame_cryptors(&self) -> Result<Vec<FrameCryptor>> {
        let mut raw = ptr::null_mut();
        check(unsafe { ffi::lk_frame_cryptor_list_create(self.as_raw(), &mut raw) })?;
        let _list = CryptorList::from_ptr(raw, &self._sdk)?;
        let mut result = Vec::new();
        for i in 0..unsafe { ffi::lk_frame_cryptor_list_count(raw) } {
            let mut info = native!(ffi::lk_frame_cryptor_info_init);
            check(unsafe { ffi::lk_frame_cryptor_list_info(raw, i, &mut info) })?;
            result.push(FrameCryptor {
                info,
                track_id: read_string(|p, n| unsafe {
                    ffi::lk_frame_cryptor_list_track_id(raw, i, p, n)
                })?,
                participant_identity: read_string(|p, n| unsafe {
                    ffi::lk_frame_cryptor_list_participant_identity(raw, i, p, n)
                })?,
            });
        }
        Ok(result)
    }
    pub fn e2ee_set_data_key_index(&self, index: usize) -> Result<()> {
        check(unsafe { ffi::lk_room_e2ee_set_data_key_index(self.as_raw(), index) })
    }
    pub fn e2ee_set_frame_cryptor_enabled(
        &self,
        track: &str,
        direction: ffi::lk_frame_cryptor_direction_t,
        enabled: bool,
    ) -> Result<()> {
        let track = cstring(track)?;
        check(unsafe {
            ffi::lk_room_e2ee_set_frame_cryptor_enabled(
                self.as_raw(),
                track.as_ptr(),
                direction,
                enabled.into(),
            )
        })
    }
    pub fn set_remote_track_subscribed(
        &self,
        participant_sid: &str,
        track_sid: &str,
        subscribed: bool,
    ) -> Result<()> {
        let p = cstring(participant_sid)?;
        let t = cstring(track_sid)?;
        check(unsafe {
            ffi::lk_room_set_remote_track_subscribed(
                self.as_raw(),
                p.as_ptr(),
                t.as_ptr(),
                subscribed.into(),
            )
        })
    }
    pub fn update_remote_track_settings(
        &self,
        participant_sid: &str,
        track_sid: &str,
        settings: &RemoteTrackSettings,
    ) -> Result<()> {
        let p = cstring(participant_sid)?;
        let t = cstring(track_sid)?;
        let mut o = native!(ffi::lk_remote_track_settings_init);
        o.enabled = settings.enabled.into();
        o.has_video_quality = settings.quality.is_some().into();
        o.video_quality = settings.quality.unwrap_or(ffi::LK_VIDEO_QUALITY_HIGH);
        o.video_width = settings.width;
        o.video_height = settings.height;
        o.video_fps = settings.fps;
        check(unsafe {
            ffi::lk_room_update_remote_track_settings(self.as_raw(), p.as_ptr(), t.as_ptr(), &o)
        })
    }
}

owned_handle!(
    CryptorList,
    ffi::lk_frame_cryptor_list_t,
    ffi::lk_frame_cryptor_list_destroy
);
#[derive(Debug, Clone)]
pub struct FrameCryptor {
    pub info: ffi::lk_frame_cryptor_info_t,
    pub track_id: String,
    pub participant_identity: String,
}

#[derive(Debug, Clone)]
pub struct TrackPermission {
    pub participant_sid: String,
    pub participant_identity: String,
    pub allow_all: bool,
    pub allowed_track_sids: Vec<String>,
}
impl Room {
    pub fn set_track_subscription_permissions(
        &self,
        all_allowed: bool,
        permissions: &[TrackPermission],
    ) -> Result<()> {
        let ids = permissions
            .iter()
            .map(|p| {
                Ok((
                    cstring(&p.participant_sid)?,
                    cstring(&p.participant_identity)?,
                ))
            })
            .collect::<Result<Vec<_>>>()?;
        let tracks = permissions
            .iter()
            .map(|p| {
                p.allowed_track_sids
                    .iter()
                    .map(|t| cstring(t))
                    .collect::<Result<Vec<_>>>()
            })
            .collect::<Result<Vec<_>>>()?;
        let pointers: Vec<Vec<_>> = tracks
            .iter()
            .map(|t| t.iter().map(|s| s.as_ptr()).collect())
            .collect();
        let native: Vec<_> = permissions
            .iter()
            .enumerate()
            .map(|(i, p)| {
                let mut o = native!(ffi::lk_participant_track_permission_init);
                o.participant_sid = ids[i].0.as_ptr();
                o.participant_identity = ids[i].1.as_ptr();
                o.allow_all = p.allow_all.into();
                o.allowed_track_sids = pointers[i].as_ptr();
                o.allowed_track_sid_count = pointers[i].len();
                o
            })
            .collect();
        check(unsafe {
            ffi::lk_room_set_track_subscription_permissions(
                self.as_raw(),
                all_allowed.into(),
                native.as_ptr(),
                native.len(),
            )
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn connection_options_keep_all_borrowed_data_alive() {
        let options = ConnectOptions {
            ice_servers: vec![IceServer {
                urls: vec!["stun:localhost:3478".into(), "turn:localhost:3478".into()],
                username: "test-user".into(),
                password: "test-only".into(),
            }],
            e2ee: Some(E2eeOptions {
                shared_key: vec![7; 32],
                key_derivation: ffi::LK_E2EE_KEY_DERIVATION_HKDF_SHA256,
            }),
            ..ConnectOptions::default()
        };
        options
            .with_native(|o| {
                unsafe {
                    assert_eq!(o.ice_server_count, 1);
                    let server = &*o.ice_servers;
                    assert_eq!(server.url_count, 2);
                    assert_eq!(copy_string(*server.urls.add(1)), "turn:localhost:3478");
                    assert_eq!(copy_string(server.username), "test-user");
                    let e2ee = &*o.e2ee_options;
                    assert_eq!(
                        std::slice::from_raw_parts(e2ee.shared_key, e2ee.shared_key_size),
                        &[7; 32]
                    );
                }
                Ok(())
            })
            .unwrap();
    }
}
