use super::*;
static LOG_IN_USE: Mutex<bool> = Mutex::new(false);
static TRACE_IN_USE: Mutex<bool> = Mutex::new(false);

#[derive(Debug, Clone)]
pub struct LogRecord {
    pub level: ffi::lk_log_level_t,
    pub source: ffi::lk_log_source_t,
    pub message: String,
    pub file: String,
    pub line: i32,
}
#[derive(Debug, Clone)]
pub struct TraceRecord {
    pub phase: ffi::lk_trace_phase_t,
    pub category: ffi::lk_trace_category_t,
    pub name: String,
    pub timestamp_us: u64,
    pub thread_id: u64,
    pub correlation_id: u64,
}
struct Queue<T> {
    sender: SyncSender<T>,
    dropped: AtomicUsize,
}
impl<T> Queue<T> {
    fn send(&self, value: T) {
        if self.sender.try_send(value).is_err() {
            self.dropped.fetch_add(1, Ordering::Relaxed);
        }
    }
}
pub struct LogEvents {
    state: Box<Queue<LogRecord>>,
    receiver: Receiver<LogRecord>,
    _sdk: Sdk,
}
pub struct TraceEvents {
    state: Box<Queue<TraceRecord>>,
    receiver: Receiver<TraceRecord>,
    _sdk: Sdk,
}
pub struct JsonTrace {
    _sdk: Sdk,
}
impl LogEvents {
    pub fn try_recv(&self) -> std::result::Result<LogRecord, TryRecvError> {
        self.receiver.try_recv()
    }
    pub fn dropped_records(&self) -> usize {
        self.state.dropped.load(Ordering::Relaxed)
    }
}
impl TraceEvents {
    pub fn try_recv(&self) -> std::result::Result<TraceRecord, TryRecvError> {
        self.receiver.try_recv()
    }
    pub fn dropped_records(&self) -> usize {
        self.state.dropped.load(Ordering::Relaxed)
    }
}
impl Drop for LogEvents {
    fn drop(&mut self) {
        let mut used = LOG_IN_USE.lock().unwrap_or_else(|e| e.into_inner());
        unsafe {
            ffi::lk_log_set_callback(None, ptr::null_mut());
        }
        *used = false;
    }
}
impl Drop for TraceEvents {
    fn drop(&mut self) {
        let mut used = TRACE_IN_USE.lock().unwrap_or_else(|e| e.into_inner());
        unsafe {
            ffi::lk_trace_stop();
        }
        *used = false;
    }
}
impl Drop for JsonTrace {
    fn drop(&mut self) {
        let mut used = TRACE_IN_USE.lock().unwrap_or_else(|e| e.into_inner());
        unsafe {
            ffi::lk_trace_stop();
        }
        *used = false;
    }
}
unsafe extern "C" fn on_log(user: *mut c_void, r: *const ffi::lk_log_record_t) {
    let _ = std::panic::catch_unwind(|| {
        let r = &*r;
        (*user.cast::<Queue<LogRecord>>()).send(LogRecord {
            level: r.level,
            source: r.source,
            message: copy_string(r.message),
            file: copy_string(r.file),
            line: r.line,
        });
    });
}
unsafe extern "C" fn on_trace(user: *mut c_void, r: *const ffi::lk_trace_record_t) {
    let _ = std::panic::catch_unwind(|| {
        let r = &*r;
        (*user.cast::<Queue<TraceRecord>>()).send(TraceRecord {
            phase: r.phase,
            category: r.category,
            name: copy_string(r.name),
            timestamp_us: r.timestamp_us,
            thread_id: r.thread_id,
            correlation_id: r.correlation_id,
        });
    });
}
impl Sdk {
    pub fn set_log_levels(
        &self,
        livekit: ffi::lk_log_level_t,
        webrtc: ffi::lk_log_level_t,
        websocket: ffi::lk_log_level_t,
    ) -> Result<()> {
        let mut options = native!(ffi::lk_log_options_init);
        options.livekit_level = livekit;
        options.webrtc_level = webrtc;
        options.websocket_level = websocket;
        check(unsafe { ffi::lk_log_set_options(&options) })
    }
    /// The SDK has one process-wide log sink. Dropping this guard unregisters it
    /// and waits for all callbacks before releasing their context.
    pub fn log_events(&self, capacity: usize) -> Result<LogEvents> {
        if capacity == 0 {
            return Err(invalid("capacity must be positive"));
        }
        let mut used = LOG_IN_USE.lock().unwrap_or_else(|e| e.into_inner());
        if *used {
            return Err(invalid("a Rust log sink is already active"));
        }
        let (sender, receiver) = mpsc::sync_channel(capacity);
        let mut state = Box::new(Queue {
            sender,
            dropped: AtomicUsize::new(0),
        });
        check(unsafe {
            ffi::lk_log_set_callback(Some(on_log), (&mut *state as *mut Queue<LogRecord>).cast())
        })?;
        *used = true;
        Ok(LogEvents {
            state,
            receiver,
            _sdk: self.clone(),
        })
    }
    pub fn trace_events(&self, capacity: usize, mask: u64) -> Result<TraceEvents> {
        if capacity == 0 {
            return Err(invalid("capacity must be positive"));
        }
        let mut used = TRACE_IN_USE.lock().unwrap_or_else(|e| e.into_inner());
        if *used {
            return Err(invalid("a Rust trace sink is already active"));
        }
        let mut options = native!(ffi::lk_trace_options_init);
        options.enabled = 1;
        options.category_mask = mask;
        check(unsafe { ffi::lk_trace_set_options(&options) })?;
        let (sender, receiver) = mpsc::sync_channel(capacity);
        let mut state = Box::new(Queue {
            sender,
            dropped: AtomicUsize::new(0),
        });
        check(unsafe {
            ffi::lk_trace_set_callback(
                Some(on_trace),
                (&mut *state as *mut Queue<TraceRecord>).cast(),
            )
        })?;
        *used = true;
        Ok(TraceEvents {
            state,
            receiver,
            _sdk: self.clone(),
        })
    }
    pub fn start_json_trace(&self, path: &str, mask: u64) -> Result<JsonTrace> {
        let path = cstring(path)?;
        let mut used = TRACE_IN_USE.lock().unwrap_or_else(|e| e.into_inner());
        if *used {
            return Err(invalid("a Rust trace sink is already active"));
        }
        let mut options = native!(ffi::lk_trace_options_init);
        options.enabled = 1;
        options.category_mask = mask;
        check(unsafe { ffi::lk_trace_set_options(&options) })?;
        check(unsafe { ffi::lk_trace_start_json_file(path.as_ptr()) })?;
        *used = true;
        Ok(JsonTrace { _sdk: self.clone() })
    }
}

owned_handle!(
    RtcStatsSnapshot,
    ffi::lk_rtc_stats_snapshot_t,
    ffi::lk_rtc_stats_snapshot_destroy
);
#[derive(Debug, Clone)]
pub struct RtcStats {
    pub info: ffi::lk_rtc_track_stats_t,
    pub id: String,
    pub kind: String,
    pub rid: String,
    pub codec_mime_type: String,
    pub codec_implementation: String,
    pub quality_limitation_reason: String,
}
impl RtcStatsSnapshot {
    pub fn entries(&self) -> Result<Vec<RtcStats>> {
        let mut result = Vec::new();
        for index in 0..unsafe { ffi::lk_rtc_stats_snapshot_count(self.as_raw()) } {
            let mut info = native!(ffi::lk_rtc_track_stats_init);
            check(unsafe { ffi::lk_rtc_stats_snapshot_info(self.as_raw(), index, &mut info) })?;
            result.push(RtcStats {
                info,
                id: read_string(|p, n| unsafe {
                    ffi::lk_rtc_stats_snapshot_id(self.as_raw(), index, p, n)
                })?,
                kind: read_string(|p, n| unsafe {
                    ffi::lk_rtc_stats_snapshot_kind(self.as_raw(), index, p, n)
                })?,
                rid: read_string(|p, n| unsafe {
                    ffi::lk_rtc_stats_snapshot_rid(self.as_raw(), index, p, n)
                })?,
                codec_mime_type: read_string(|p, n| unsafe {
                    ffi::lk_rtc_stats_snapshot_codec_mime_type(self.as_raw(), index, p, n)
                })?,
                codec_implementation: read_string(|p, n| unsafe {
                    ffi::lk_rtc_stats_snapshot_codec_implementation(self.as_raw(), index, p, n)
                })?,
                quality_limitation_reason: read_string(|p, n| unsafe {
                    ffi::lk_rtc_stats_snapshot_quality_limitation_reason(self.as_raw(), index, p, n)
                })?,
            });
        }
        Ok(result)
    }
}
impl LocalTrack {
    pub fn rtc_stats_snapshot(&self) -> Result<RtcStatsSnapshot> {
        let mut raw = ptr::null_mut();
        check(unsafe { ffi::lk_local_track_create_rtc_stats_snapshot(self.as_raw(), &mut raw) })?;
        RtcStatsSnapshot::from_ptr(raw, self.sdk())
    }
}
