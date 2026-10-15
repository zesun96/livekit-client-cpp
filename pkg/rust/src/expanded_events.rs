use super::*;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ReceivedStreamInfo {
    pub stream_id: String,
    pub mime_type: String,
    pub name: String,
    pub topic: String,
    pub participant_identity: String,
    pub attributes: Vec<(String, String)>,
    pub reply_to_stream_id: String,
    pub attached_stream_ids: Vec<String>,
    pub timestamp: i64,
}
pub(super) unsafe fn completed_text_info(u: *mut c_void, p: *const ffi::lk_text_received_t) {
    emit(u, || {
        let p = &*p;
        Event::IncomingStreamInfo(ReceivedStreamInfo {
            stream_id: copy_string(p.stream_id),
            mime_type: "text/plain".into(),
            name: String::new(),
            topic: copy_string(p.topic),
            participant_identity: copy_string(p.participant_identity),
            attributes: attributes(p.attributes, p.attribute_count),
            reply_to_stream_id: copy_string(p.reply_to_stream_id),
            attached_stream_ids: slice(p.attached_stream_ids, p.attached_stream_id_count)
                .iter()
                .map(|&s| copy_string(s))
                .collect(),
            timestamp: p.timestamp,
        })
    });
}
pub(super) unsafe fn completed_byte_info(u: *mut c_void, p: *const ffi::lk_file_received_t) {
    emit(u, || {
        let p = &*p;
        Event::IncomingStreamInfo(ReceivedStreamInfo {
            stream_id: copy_string(p.stream_id),
            mime_type: copy_string(p.mime_type),
            name: copy_string(p.name),
            topic: copy_string(p.topic),
            participant_identity: copy_string(p.participant_identity),
            attributes: attributes(p.attributes, p.attribute_count),
            reply_to_stream_id: String::new(),
            attached_stream_ids: Vec::new(),
            timestamp: p.timestamp,
        })
    });
}
pub(super) unsafe fn text_stream_info(u: *mut c_void, p: *const ffi::lk_text_stream_event_t) {
    if (*p).type_ != ffi::LK_DATA_STREAM_EVENT_CHUNK {
        emit(u, || {
            let p = &*p;
            Event::IncomingStreamInfo(ReceivedStreamInfo {
                stream_id: copy_string(p.stream_id),
                mime_type: copy_string(p.mime_type),
                name: String::new(),
                topic: copy_string(p.topic),
                participant_identity: copy_string(p.participant_identity),
                attributes: attributes(p.attributes, p.attribute_count),
                reply_to_stream_id: copy_string(p.reply_to_stream_id),
                attached_stream_ids: slice(p.attached_stream_ids, p.attached_stream_id_count)
                    .iter()
                    .map(|&s| copy_string(s))
                    .collect(),
                timestamp: p.timestamp,
            })
        });
    }
}
pub(super) unsafe fn byte_stream_info(u: *mut c_void, p: *const ffi::lk_byte_stream_event_t) {
    if (*p).type_ != ffi::LK_DATA_STREAM_EVENT_CHUNK {
        emit(u, || {
            let p = &*p;
            Event::IncomingStreamInfo(ReceivedStreamInfo {
                stream_id: copy_string(p.stream_id),
                mime_type: copy_string(p.mime_type),
                name: copy_string(p.name),
                topic: copy_string(p.topic),
                participant_identity: copy_string(p.participant_identity),
                attributes: attributes(p.attributes, p.attribute_count),
                reply_to_stream_id: String::new(),
                attached_stream_ids: Vec::new(),
                timestamp: p.timestamp,
            })
        });
    }
}

unsafe fn slice<'a, T>(p: *const T, n: usize) -> &'a [T] {
    if n == 0 {
        &[]
    } else {
        std::slice::from_raw_parts(p, n)
    }
}
pub(super) unsafe fn attributes(p: *const ffi::lk_attribute_t, n: usize) -> Vec<(String, String)> {
    slice(p, n)
        .iter()
        .map(|a| (copy_string(a.key), copy_string(a.value)))
        .collect()
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Permissions {
    pub can_subscribe: bool,
    pub can_publish: bool,
    pub can_publish_data: bool,
    pub can_publish_sources: Vec<ffi::lk_track_source_t>,
    pub hidden: bool,
    pub recorder: bool,
    pub can_update_metadata: bool,
    pub agent: bool,
    pub can_subscribe_metrics: bool,
    pub can_manage_agent_session: bool,
}
pub(super) unsafe fn permissions(p: *const ffi::lk_participant_permissions_t) -> Permissions {
    let p = &*p;
    Permissions {
        can_subscribe: p.can_subscribe != 0,
        can_publish: p.can_publish != 0,
        can_publish_data: p.can_publish_data != 0,
        can_publish_sources: slice(p.can_publish_sources, p.can_publish_source_count).to_vec(),
        hidden: p.hidden != 0,
        recorder: p.recorder != 0,
        can_update_metadata: p.can_update_metadata != 0,
        agent: p.agent != 0,
        can_subscribe_metrics: p.can_subscribe_metrics != 0,
        can_manage_agent_session: p.can_manage_agent_session != 0,
    }
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RoomSnapshot {
    pub sid: String,
    pub name: String,
    pub metadata: String,
    pub is_recording: bool,
}
unsafe fn room_snapshot(p: *const ffi::lk_room_snapshot_t) -> RoomSnapshot {
    let p = &*p;
    RoomSnapshot {
        sid: copy_string(p.sid),
        name: copy_string(p.name),
        metadata: copy_string(p.metadata),
        is_recording: p.is_recording != 0,
    }
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RoomEvent {
    Connected,
    Disconnected(ffi::lk_disconnect_reason_t),
    Reconnecting,
    Reconnected,
    TokenRefreshed,
    Eos,
    MetadataChanged(String),
    SidChanged { previous: String, current: String },
    Updated(RoomSnapshot),
    Moved(RoomSnapshot),
    RecordingChanged(bool),
    ActiveSpeakers(Vec<Participant>),
    ParticipantsUpdated(Vec<Participant>),
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ParticipantEvent {
    Metadata {
        previous: String,
        participant: Participant,
    },
    Name {
        name: String,
        participant: Participant,
    },
    Attributes {
        changes: Vec<(String, String)>,
        participant: Participant,
    },
    Permissions {
        previous: Permissions,
        current: Permissions,
        participant: Participant,
    },
    ConnectionQuality {
        quality: ffi::lk_connection_quality_t,
        participant: Participant,
    },
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SubscribedQuality {
    pub quality: ffi::lk_video_quality_t,
    pub enabled: bool,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SubscribedCodec {
    pub codec: String,
    pub qualities: Vec<SubscribedQuality>,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TrackFeedback {
    Unpublished,
    Muted,
    Unmuted,
    LocalPublished,
    LocalUnpublished,
    LocalSubscribed,
    PermissionChanged(bool),
    SubscriptionFailed(ffi::lk_subscription_error_t),
    StreamStateChanged(ffi::lk_track_stream_state_t),
    SubscriptionStatusChanged(ffi::lk_track_subscription_status_t),
    SubscribedQuality {
        track_sid: String,
        qualities: Vec<SubscribedQuality>,
        codecs: Vec<SubscribedCodec>,
    },
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ChatEvent {
    pub id: String,
    pub timestamp: i64,
    pub edit_timestamp: Option<i64>,
    pub message: String,
    pub deleted: bool,
    pub generated: bool,
    pub participant_identity: String,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TranscriptionSegment {
    pub id: String,
    pub text: String,
    pub language: String,
    pub start_time: u64,
    pub end_time: u64,
    pub is_final: bool,
    pub first_received_time: i64,
    pub last_received_time: i64,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TranscriptionEvent {
    pub participant_identity: String,
    pub track_id: String,
    pub segments: Vec<TranscriptionSegment>,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EncryptionState {
    pub track_id: String,
    pub participant_identity: String,
    pub kind: ffi::lk_track_kind_t,
    pub direction: ffi::lk_frame_cryptor_direction_t,
    pub enabled: bool,
    pub key_index: usize,
    pub state: ffi::lk_frame_cryptor_state_t,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DataTrackInfo {
    pub publisher_handle: u16,
    pub sid: String,
    pub name: String,
    pub uses_e2ee: bool,
    pub frame_encoding: Option<ffi::lk_data_track_frame_encoding_t>,
    pub custom_frame_encoding: String,
    pub schema_name: Option<String>,
    pub schema_encoding: ffi::lk_data_track_schema_encoding_t,
    pub custom_schema_encoding: String,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum DataTrackChange {
    Published,
    Unpublished,
    LocalPublished,
    LocalUnpublished,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StreamCompletion {
    pub status: ffi::lk_data_stream_completion_status_t,
    pub stream_id: String,
    pub bytes_sent: u64,
    pub total_size: Option<u64>,
    pub reason: String,
    pub error_domain: ffi::lk_error_domain_t,
    pub error_code: i32,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MetricSample {
    pub timestamp_ms: i64,
    pub normalized_timestamp: Option<(i64, i32)>,
    pub value_bits: u32,
}
impl MetricSample {
    pub fn value(&self) -> f32 {
        f32::from_bits(self.value_bits)
    }
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TimeSeriesMetric {
    pub label: u32,
    pub participant_identity: u32,
    pub track_sid: u32,
    pub rid: u32,
    pub samples: Vec<MetricSample>,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EventMetric {
    pub label: u32,
    pub participant_identity: u32,
    pub track_sid: u32,
    pub rid: u32,
    pub start_timestamp_ms: i64,
    pub end_timestamp_ms: Option<i64>,
    pub normalized_start_timestamp: Option<(i64, i32)>,
    pub normalized_end_timestamp: Option<(i64, i32)>,
    pub metadata: String,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MetricsEvent {
    pub timestamp_ms: i64,
    pub normalized_timestamp: Option<(i64, i32)>,
    pub string_data: Vec<String>,
    pub time_series: Vec<TimeSeriesMetric>,
    pub events: Vec<EventMetric>,
    pub participant_identity: String,
}

macro_rules! room_callback {
    ($fn:ident,$event:ident) => {
        unsafe extern "C" fn $fn(u: *mut c_void, _: *mut ffi::lk_room_t) {
            emit(u, || Event::Room(RoomEvent::$event));
        }
    };
}
room_callback!(connected, Connected);
room_callback!(reconnecting, Reconnecting);
room_callback!(reconnected, Reconnected);
room_callback!(token_refreshed, TokenRefreshed);
room_callback!(eos, Eos);
unsafe extern "C" fn disconnected(
    u: *mut c_void,
    _: *mut ffi::lk_room_t,
    r: ffi::lk_disconnect_reason_t,
) {
    emit(u, || Event::Room(RoomEvent::Disconnected(r)));
}
unsafe extern "C" fn metadata(u: *mut c_void, _: *mut ffi::lk_room_t, s: *const c_char) {
    emit(u, || {
        Event::Room(RoomEvent::MetadataChanged(copy_string(s)))
    });
}
unsafe extern "C" fn sid(
    u: *mut c_void,
    _: *mut ffi::lk_room_t,
    p: *const c_char,
    s: *const c_char,
) {
    emit(u, || {
        Event::Room(RoomEvent::SidChanged {
            previous: copy_string(p),
            current: copy_string(s),
        })
    });
}
unsafe extern "C" fn updated(
    u: *mut c_void,
    _: *mut ffi::lk_room_t,
    s: *const ffi::lk_room_snapshot_t,
) {
    emit(u, || Event::Room(RoomEvent::Updated(room_snapshot(s))));
}
unsafe extern "C" fn moved(
    u: *mut c_void,
    _: *mut ffi::lk_room_t,
    s: *const ffi::lk_room_snapshot_t,
) {
    emit(u, || Event::Room(RoomEvent::Moved(room_snapshot(s))));
}
unsafe extern "C" fn recording(u: *mut c_void, _: *mut ffi::lk_room_t, b: i32) {
    emit(u, || Event::Room(RoomEvent::RecordingChanged(b != 0)));
}
unsafe extern "C" fn speakers(
    u: *mut c_void,
    _: *mut ffi::lk_room_t,
    p: *const ffi::lk_participant_info_t,
    n: usize,
) {
    emit(u, || {
        Event::Room(RoomEvent::ActiveSpeakers(
            slice(p, n).iter().map(|p| participant(p)).collect(),
        ))
    });
}
unsafe extern "C" fn participants_updated(
    u: *mut c_void,
    _: *mut ffi::lk_room_t,
    p: *const ffi::lk_participant_info_t,
    n: usize,
) {
    emit(u, || {
        Event::Room(RoomEvent::ParticipantsUpdated(
            slice(p, n).iter().map(|p| participant(p)).collect(),
        ))
    });
}
unsafe extern "C" fn participant_metadata(
    u: *mut c_void,
    _: *mut ffi::lk_room_t,
    s: *const c_char,
    p: *const ffi::lk_participant_info_t,
) {
    emit(u, || {
        Event::ParticipantChanged(ParticipantEvent::Metadata {
            previous: copy_string(s),
            participant: participant(p),
        })
    });
}
unsafe extern "C" fn participant_name(
    u: *mut c_void,
    _: *mut ffi::lk_room_t,
    s: *const c_char,
    p: *const ffi::lk_participant_info_t,
) {
    emit(u, || {
        Event::ParticipantChanged(ParticipantEvent::Name {
            name: copy_string(s),
            participant: participant(p),
        })
    });
}
unsafe extern "C" fn participant_attributes(
    u: *mut c_void,
    _: *mut ffi::lk_room_t,
    a: *const ffi::lk_attribute_t,
    n: usize,
    p: *const ffi::lk_participant_info_t,
) {
    emit(u, || {
        Event::ParticipantChanged(ParticipantEvent::Attributes {
            changes: attributes(a, n),
            participant: participant(p),
        })
    });
}
unsafe extern "C" fn participant_permissions(
    u: *mut c_void,
    _: *mut ffi::lk_room_t,
    old: *const ffi::lk_participant_permissions_t,
    new: *const ffi::lk_participant_permissions_t,
    p: *const ffi::lk_participant_info_t,
) {
    emit(u, || {
        Event::ParticipantChanged(ParticipantEvent::Permissions {
            previous: permissions(old),
            current: permissions(new),
            participant: participant(p),
        })
    });
}
unsafe extern "C" fn quality(
    u: *mut c_void,
    _: *mut ffi::lk_room_t,
    q: ffi::lk_connection_quality_t,
    p: *const ffi::lk_participant_info_t,
) {
    emit(u, || {
        Event::ParticipantChanged(ParticipantEvent::ConnectionQuality {
            quality: q,
            participant: participant(p),
        })
    });
}
macro_rules! track_callback {
    ($fn:ident,$event:ident) => {
        unsafe extern "C" fn $fn(
            u: *mut c_void,
            _: *mut ffi::lk_room_t,
            t: *const ffi::lk_track_publication_info_t,
            p: *const ffi::lk_participant_info_t,
        ) {
            emit(u, || Event::TrackFeedback {
                track: events::track_info(t, p),
                feedback: TrackFeedback::$event,
            });
        }
    };
}
track_callback!(unpublished, Unpublished);
track_callback!(muted, Muted);
track_callback!(unmuted, Unmuted);
track_callback!(local_published, LocalPublished);
track_callback!(local_unpublished, LocalUnpublished);
track_callback!(local_subscribed, LocalSubscribed);
macro_rules! feedback_callback {
    ($fn:ident,$ty:ty,$variant:ident,$convert:expr) => {
        unsafe extern "C" fn $fn(
            u: *mut c_void,
            _: *mut ffi::lk_room_t,
            t: *const ffi::lk_track_publication_info_t,
            p: *const ffi::lk_participant_info_t,
            v: $ty,
        ) {
            emit(u, || Event::TrackFeedback {
                track: events::track_info(t, p),
                feedback: TrackFeedback::$variant(($convert)(v)),
            });
        }
    };
}
feedback_callback!(permission, i32, PermissionChanged, |v| v != 0);
feedback_callback!(
    failed,
    ffi::lk_subscription_error_t,
    SubscriptionFailed,
    |v| v
);
feedback_callback!(
    stream_state,
    ffi::lk_track_stream_state_t,
    StreamStateChanged,
    |v| v
);
feedback_callback!(
    subscription,
    ffi::lk_track_subscription_status_t,
    SubscriptionStatusChanged,
    |v| v
);
unsafe fn qualities(p: *const ffi::lk_subscribed_quality_t, n: usize) -> Vec<SubscribedQuality> {
    slice(p, n)
        .iter()
        .map(|q| SubscribedQuality {
            quality: q.quality,
            enabled: q.enabled != 0,
        })
        .collect()
}
unsafe extern "C" fn subscribed_quality(
    u: *mut c_void,
    _: *mut ffi::lk_room_t,
    t: *const ffi::lk_track_publication_info_t,
    p: *const ffi::lk_participant_info_t,
    q: *const ffi::lk_subscribed_quality_update_t,
) {
    emit(u, || {
        let q = &*q;
        Event::TrackFeedback {
            track: events::track_info(t, p),
            feedback: TrackFeedback::SubscribedQuality {
                track_sid: copy_string(q.track_sid),
                qualities: qualities(q.qualities, q.quality_count),
                codecs: slice(q.codecs, q.codec_count)
                    .iter()
                    .map(|c| SubscribedCodec {
                        codec: copy_string(c.codec),
                        qualities: qualities(c.qualities, c.quality_count),
                    })
                    .collect(),
            },
        }
    });
}
unsafe extern "C" fn chat(
    u: *mut c_void,
    _: *mut ffi::lk_room_t,
    c: *const ffi::lk_chat_message_t,
) {
    emit(u, || {
        let c = &*c;
        Event::Chat(ChatEvent {
            id: copy_string(c.id),
            timestamp: c.timestamp,
            edit_timestamp: (c.has_edit_timestamp != 0).then_some(c.edit_timestamp),
            message: copy_string(c.message),
            deleted: c.deleted != 0,
            generated: c.generated != 0,
            participant_identity: copy_string(c.participant_identity),
        })
    });
}
unsafe extern "C" fn dtmf(u: *mut c_void, _: *mut ffi::lk_room_t, d: *const ffi::lk_sip_dtmf_t) {
    emit(u, || {
        let d = &*d;
        Event::SipDtmf {
            code: d.code,
            digit: copy_string(d.digit),
            participant_identity: copy_string(d.participant_identity),
        }
    });
}
unsafe extern "C" fn transcription(
    u: *mut c_void,
    _: *mut ffi::lk_room_t,
    t: *const ffi::lk_transcription_received_t,
) {
    emit(u, || {
        let t = &*t;
        Event::Transcription(TranscriptionEvent {
            participant_identity: copy_string(t.transcribed_participant_identity),
            track_id: copy_string(t.track_id),
            segments: slice(t.segments, t.segment_count)
                .iter()
                .map(|s| TranscriptionSegment {
                    id: copy_string(s.id),
                    text: copy_string(s.text),
                    language: copy_string(s.language),
                    start_time: s.start_time,
                    end_time: s.end_time,
                    is_final: s.is_final != 0,
                    first_received_time: s.first_received_time,
                    last_received_time: s.last_received_time,
                })
                .collect(),
        })
    });
}
unsafe extern "C" fn encryption(
    u: *mut c_void,
    _: *mut ffi::lk_room_t,
    e: *const ffi::lk_encryption_state_t,
) {
    emit(u, || {
        let e = &*e;
        Event::EncryptionChanged(EncryptionState {
            track_id: copy_string(e.track_id),
            participant_identity: copy_string(e.participant_identity),
            kind: e.kind,
            direction: e.direction,
            enabled: e.enabled != 0,
            key_index: e.key_index,
            state: e.state,
        })
    });
}
unsafe fn data_info(t: *const ffi::lk_data_track_info_t) -> DataTrackInfo {
    let t = &*t;
    DataTrackInfo {
        publisher_handle: t.publisher_handle,
        sid: copy_string(t.sid),
        name: copy_string(t.name),
        uses_e2ee: t.uses_e2ee != 0,
        frame_encoding: (t.has_frame_encoding != 0).then_some(t.frame_encoding),
        custom_frame_encoding: copy_string(t.custom_frame_encoding),
        schema_name: (t.has_schema != 0).then(|| copy_string(t.schema_name)),
        schema_encoding: t.schema_encoding,
        custom_schema_encoding: copy_string(t.custom_schema_encoding),
    }
}
macro_rules! data_callback {
    ($fn:ident,$event:ident) => {
        unsafe extern "C" fn $fn(
            u: *mut c_void,
            _: *mut ffi::lk_room_t,
            t: *const ffi::lk_data_track_info_t,
            p: *const ffi::lk_participant_info_t,
        ) {
            emit(u, || Event::DataTrackChanged {
                track: data_info(t),
                participant: participant(p),
                change: DataTrackChange::$event,
            });
        }
    };
}
data_callback!(data_published, Published);
data_callback!(data_unpublished, Unpublished);
data_callback!(local_data_published, LocalPublished);
data_callback!(local_data_unpublished, LocalUnpublished);
unsafe extern "C" fn buffer_status(
    u: *mut c_void,
    _: *mut ffi::lk_room_t,
    s: *const ffi::lk_data_channel_buffer_status_t,
) {
    emit(u, || {
        let s = &*s;
        Event::BufferStatus {
            reliable: s.reliable != 0,
            buffered_amount: s.buffered_amount,
            high_water_mark: s.high_water_mark,
            low_water_mark: s.low_water_mark,
            backpressured: s.backpressured != 0,
        }
    });
}
unsafe extern "C" fn metrics(
    u: *mut c_void,
    _: *mut ffi::lk_room_t,
    m: *const ffi::lk_metrics_received_t,
) {
    emit(u, || {
        let m = &*m;
        Event::Metrics(MetricsEvent {
            timestamp_ms: m.timestamp_ms,
            normalized_timestamp: (m.has_normalized_timestamp != 0)
                .then_some((m.normalized_timestamp.seconds, m.normalized_timestamp.nanos)),
            string_data: slice(m.string_data, m.string_data_count)
                .iter()
                .map(|&s| copy_string(s))
                .collect(),
            time_series: slice(m.time_series, m.time_series_count)
                .iter()
                .map(|s| TimeSeriesMetric {
                    label: s.label,
                    participant_identity: s.participant_identity,
                    track_sid: s.track_sid,
                    rid: s.rid,
                    samples: slice(s.samples, s.sample_count)
                        .iter()
                        .map(|v| MetricSample {
                            timestamp_ms: v.timestamp_ms,
                            normalized_timestamp: (v.has_normalized_timestamp != 0).then_some((
                                v.normalized_timestamp.seconds,
                                v.normalized_timestamp.nanos,
                            )),
                            value_bits: v.value.to_bits(),
                        })
                        .collect(),
                })
                .collect(),
            events: slice(m.events, m.event_count)
                .iter()
                .map(|e| EventMetric {
                    label: e.label,
                    participant_identity: e.participant_identity,
                    track_sid: e.track_sid,
                    rid: e.rid,
                    start_timestamp_ms: e.start_timestamp_ms,
                    end_timestamp_ms: (e.has_end_timestamp_ms != 0).then_some(e.end_timestamp_ms),
                    normalized_start_timestamp: (e.has_normalized_start_timestamp != 0).then_some(
                        (
                            e.normalized_start_timestamp.seconds,
                            e.normalized_start_timestamp.nanos,
                        ),
                    ),
                    normalized_end_timestamp: (e.has_normalized_end_timestamp != 0).then_some((
                        e.normalized_end_timestamp.seconds,
                        e.normalized_end_timestamp.nanos,
                    )),
                    metadata: copy_string(e.metadata),
                })
                .collect(),
            participant_identity: copy_string(m.participant_identity),
        })
    });
}

pub(super) fn install(c: &mut ffi::lk_room_callbacks_t) {
    c.on_connected = Some(connected);
    c.on_disconnected_with_reason = Some(disconnected);
    c.on_reconnecting = Some(reconnecting);
    c.on_reconnected = Some(reconnected);
    c.on_token_refreshed = Some(token_refreshed);
    c.on_room_eos = Some(eos);
    c.on_room_metadata_changed = Some(metadata);
    c.on_room_sid_changed = Some(sid);
    c.on_room_updated = Some(updated);
    c.on_room_moved = Some(moved);
    c.on_recording_status_changed = Some(recording);
    c.on_active_speakers_changed = Some(speakers);
    c.on_participants_updated = Some(participants_updated);
    c.on_participant_metadata_changed = Some(participant_metadata);
    c.on_participant_name_changed = Some(participant_name);
    c.on_participant_attributes_changed = Some(participant_attributes);
    c.on_participant_permissions_changed = Some(participant_permissions);
    c.on_connection_quality_changed = Some(quality);
    c.on_track_unpublished = Some(unpublished);
    c.on_track_muted = Some(muted);
    c.on_track_unmuted = Some(unmuted);
    c.on_local_track_published = Some(local_published);
    c.on_local_track_unpublished = Some(local_unpublished);
    c.on_local_track_subscribed = Some(local_subscribed);
    c.on_track_subscription_permission_changed = Some(permission);
    c.on_track_subscription_failed = Some(failed);
    c.on_track_stream_state_changed = Some(stream_state);
    c.on_track_subscription_status_changed = Some(subscription);
    c.on_subscribed_quality_update = Some(subscribed_quality);
    c.on_chat_message_received = Some(chat);
    c.on_sip_dtmf_received = Some(dtmf);
    c.on_transcription_received = Some(transcription);
    c.on_encryption_state_changed = Some(encryption);
    c.on_metrics_received = Some(metrics);
    c.on_data_channel_buffer_status_changed = Some(buffer_status);
    c.on_data_track_published = Some(data_published);
    c.on_data_track_unpublished = Some(data_unpublished);
    c.on_local_data_track_published = Some(local_data_published);
    c.on_local_data_track_unpublished = Some(local_data_unpublished);
    c.on_file_received = Some(events::on_bytes);
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn callbacks_copy_nested_permissions_and_chat_strings() {
        let (sender, receiver) = mpsc::sync_channel(4);
        let state = CallbackState {
            sender,
            dropped: Arc::new(AtomicUsize::new(0)),
            notifier: Mutex::new(None),
        };
        let user = (&state as *const CallbackState).cast_mut().cast();
        let mut sources = vec![ffi::LK_TRACK_SOURCE_CAMERA];
        let mut native: ffi::lk_participant_permissions_t = unsafe { std::mem::zeroed() };
        native.can_publish_sources = sources.as_ptr();
        native.can_publish_source_count = 1;
        native.can_publish = 1;
        let p = ffi::lk_participant_info_t {
            identity: c"peer".as_ptr(),
            ..unsafe { std::mem::zeroed() }
        };
        unsafe {
            participant_permissions(user, ptr::null_mut(), &native, &native, &p);
        }
        sources.clear();
        drop(sources);
        match receiver.try_recv().unwrap() {
            Event::ParticipantChanged(ParticipantEvent::Permissions {
                current,
                participant,
                ..
            }) => {
                assert_eq!(current.can_publish_sources, [ffi::LK_TRACK_SOURCE_CAMERA]);
                assert!(current.can_publish);
                assert_eq!(participant.identity, "peer");
            }
            _ => panic!("wrong event"),
        }
        let message = CString::new("chat payload").unwrap();
        let event = ffi::lk_chat_message_t {
            id: c"id".as_ptr(),
            message: message.as_ptr(),
            has_edit_timestamp: 1,
            edit_timestamp: 42,
            ..unsafe { std::mem::zeroed() }
        };
        unsafe {
            chat(user, ptr::null_mut(), &event);
        }
        drop(message);
        match receiver.try_recv().unwrap() {
            Event::Chat(event) => {
                assert_eq!(event.message, "chat payload");
                assert_eq!(event.edit_timestamp, Some(42));
            }
            _ => panic!("wrong event"),
        }
        let t = ffi::lk_track_publication_info_t {
            sid: c"track".as_ptr(),
            ..unsafe { std::mem::zeroed() }
        };
        unsafe {
            permission(user, ptr::null_mut(), &t, &p, 0);
        }
        assert!(matches!(
            receiver.try_recv().unwrap(),
            Event::TrackFeedback {
                feedback: TrackFeedback::PermissionChanged(false),
                ..
            }
        ));
    }
}
