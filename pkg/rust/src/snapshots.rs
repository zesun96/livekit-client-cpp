use super::*;

owned_handle!(
    WriterInfoHandle,
    ffi::lk_data_stream_writer_info_snapshot_t,
    ffi::lk_data_stream_writer_info_snapshot_destroy
);
owned_handle!(
    DataTrackList,
    ffi::lk_remote_data_track_list_t,
    ffi::lk_remote_data_track_list_destroy
);
#[derive(Debug, Clone)]
pub struct ParticipantSnapshot {
    pub participant: Participant,
    pub attributes: Vec<(String, String)>,
    pub permissions: Permissions,
    pub audio_level: f32,
    pub connection_quality: ffi::lk_connection_quality_t,
    pub is_speaking: bool,
    pub publications: Vec<PublicationSnapshot>,
}
#[derive(Debug, Clone)]
pub struct PublicationSnapshot {
    pub sid: String,
    pub name: String,
    pub mime_type: String,
    pub info: ffi::lk_remote_track_publication_snapshot_info_t,
    pub track: Option<RemoteTrackSnapshot>,
}
#[derive(Debug, Clone)]
pub struct RemoteTrackSnapshot {
    pub sid: String,
    pub name: String,
    pub info: ffi::lk_remote_track_snapshot_info_t,
}
#[derive(Debug, Clone)]
pub struct RemoteDataTrack {
    pub participant_identity: String,
    pub sid: String,
    pub name: String,
    pub info: ffi::lk_data_track_snapshot_info_t,
    pub custom_frame_encoding: String,
    pub schema_name: String,
    pub custom_schema_encoding: String,
}
#[derive(Debug, Clone)]
pub struct StreamInfo {
    pub kind: ffi::lk_data_stream_writer_kind_t,
    pub total_size: Option<u64>,
    pub timestamp: i64,
    pub stream_id: String,
    pub mime_type: String,
    pub topic: String,
    pub participant_identity: String,
    pub name: String,
    pub reply_to_stream_id: String,
    pub attributes: Vec<(String, String)>,
    pub attached_stream_ids: Vec<String>,
}

impl Room {
    pub fn local_participant_snapshot(&self) -> Result<ParticipantSnapshot> {
        unsafe {
            let mut p = ptr::null_mut();
            check(ffi::lk_room_create_local_participant_snapshot(
                self.as_raw(),
                &mut p,
            ))?;
            let _owner = LocalSnapshot(p);
            let mut info = native!(ffi::lk_local_participant_snapshot_info_init);
            check(ffi::lk_local_participant_snapshot_info(p, &mut info))?;
            let mut permissions = MaybeUninit::uninit();
            check(ffi::lk_local_participant_snapshot_permissions(
                p,
                permissions.as_mut_ptr(),
            ))?;
            let participant = Participant {
                sid: read_string(|b, n| ffi::lk_local_participant_snapshot_sid(p, b, n))?,
                identity: read_string(|b, n| ffi::lk_local_participant_snapshot_identity(p, b, n))?,
                name: read_string(|b, n| ffi::lk_local_participant_snapshot_name(p, b, n))?,
                metadata: read_string(|b, n| ffi::lk_local_participant_snapshot_metadata(p, b, n))?,
            };
            let mut attributes = Vec::new();
            for i in 0..ffi::lk_local_participant_snapshot_attribute_count(p) {
                attributes.push((
                    read_string(|b, n| {
                        ffi::lk_local_participant_snapshot_attribute_key(p, i, b, n)
                    })?,
                    read_string(|b, n| {
                        ffi::lk_local_participant_snapshot_attribute_value(p, i, b, n)
                    })?,
                ));
            }
            Ok(ParticipantSnapshot {
                participant,
                attributes,
                permissions: expanded_events::permissions(&permissions.assume_init()),
                audio_level: info.audio_level,
                connection_quality: info.connection_quality,
                is_speaking: info.is_speaking != 0,
                publications: Vec::new(),
            })
        }
    }
    pub fn remote_participant_snapshots(&self) -> Result<Vec<ParticipantSnapshot>> {
        unsafe {
            let mut raw = ptr::null_mut();
            check(ffi::lk_room_create_remote_participant_snapshot(
                self.as_raw(),
                &mut raw,
            ))?;
            let _owner = RemoteSnapshot(raw);
            let mut result = Vec::new();
            for index in 0..ffi::lk_remote_participant_list_count(raw) {
                let mut p = ptr::null();
                check(ffi::lk_remote_participant_list_at(raw, index, &mut p))?;
                let mut info = native!(ffi::lk_remote_participant_snapshot_info_init);
                check(ffi::lk_remote_participant_snapshot_info(p, &mut info))?;
                let mut permissions = MaybeUninit::uninit();
                check(ffi::lk_remote_participant_snapshot_permissions(
                    p,
                    permissions.as_mut_ptr(),
                ))?;
                let participant = Participant {
                    sid: read_string(|b, n| ffi::lk_remote_participant_snapshot_sid(p, b, n))?,
                    identity: read_string(|b, n| {
                        ffi::lk_remote_participant_snapshot_identity(p, b, n)
                    })?,
                    name: read_string(|b, n| ffi::lk_remote_participant_snapshot_name(p, b, n))?,
                    metadata: read_string(|b, n| {
                        ffi::lk_remote_participant_snapshot_metadata(p, b, n)
                    })?,
                };
                let mut attributes = Vec::new();
                for i in 0..ffi::lk_remote_participant_snapshot_attribute_count(p) {
                    attributes.push((
                        read_string(|b, n| {
                            ffi::lk_remote_participant_snapshot_attribute_key(p, i, b, n)
                        })?,
                        read_string(|b, n| {
                            ffi::lk_remote_participant_snapshot_attribute_value(p, i, b, n)
                        })?,
                    ));
                }
                let mut publications = Vec::new();
                for i in 0..ffi::lk_remote_participant_snapshot_publication_count(p) {
                    let mut t = ptr::null();
                    check(ffi::lk_remote_participant_snapshot_publication_at(
                        p, i, &mut t,
                    ))?;
                    let mut info = native!(ffi::lk_remote_track_publication_snapshot_info_init);
                    check(ffi::lk_remote_track_publication_snapshot_info(t, &mut info))?;
                    let track = if info.has_subscribed_track != 0 {
                        let mut track = ptr::null();
                        check(ffi::lk_remote_track_publication_snapshot_track(
                            t, &mut track,
                        ))?;
                        let mut ti = native!(ffi::lk_remote_track_snapshot_info_init);
                        check(ffi::lk_remote_track_snapshot_info(track, &mut ti))?;
                        Some(RemoteTrackSnapshot {
                            sid: read_string(|b, n| {
                                ffi::lk_remote_track_snapshot_sid(track, b, n)
                            })?,
                            name: read_string(|b, n| {
                                ffi::lk_remote_track_snapshot_name(track, b, n)
                            })?,
                            info: ti,
                        })
                    } else {
                        None
                    };
                    publications.push(PublicationSnapshot {
                        sid: read_string(|b, n| {
                            ffi::lk_remote_track_publication_snapshot_sid(t, b, n)
                        })?,
                        name: read_string(|b, n| {
                            ffi::lk_remote_track_publication_snapshot_name(t, b, n)
                        })?,
                        mime_type: read_string(|b, n| {
                            ffi::lk_remote_track_publication_snapshot_mime_type(t, b, n)
                        })?,
                        info,
                        track,
                    });
                }
                result.push(ParticipantSnapshot {
                    participant,
                    attributes,
                    permissions: expanded_events::permissions(&permissions.assume_init()),
                    audio_level: info.audio_level,
                    connection_quality: info.connection_quality,
                    is_speaking: info.is_speaking != 0,
                    publications,
                });
            }
            Ok(result)
        }
    }
    pub fn remote_rtc_stats(
        &self,
        participant_identity: &str,
        track_sid: &str,
    ) -> Result<Vec<RtcStats>> {
        unsafe {
            let mut raw = ptr::null_mut();
            check(ffi::lk_room_create_remote_participant_snapshot(
                self.as_raw(),
                &mut raw,
            ))?;
            let _owner = RemoteSnapshot(raw);
            for i in 0..ffi::lk_remote_participant_list_count(raw) {
                let mut p = ptr::null();
                check(ffi::lk_remote_participant_list_at(raw, i, &mut p))?;
                if read_string(|b, n| ffi::lk_remote_participant_snapshot_identity(p, b, n))?
                    != participant_identity
                {
                    continue;
                }
                for j in 0..ffi::lk_remote_participant_snapshot_publication_count(p) {
                    let mut publication = ptr::null();
                    check(ffi::lk_remote_participant_snapshot_publication_at(
                        p,
                        j,
                        &mut publication,
                    ))?;
                    if read_string(|b, n| {
                        ffi::lk_remote_track_publication_snapshot_sid(publication, b, n)
                    })? != track_sid
                    {
                        continue;
                    }
                    let mut track = ptr::null();
                    check(ffi::lk_remote_track_publication_snapshot_track(
                        publication,
                        &mut track,
                    ))?;
                    let mut stats = ptr::null_mut();
                    check(ffi::lk_remote_track_snapshot_create_rtc_stats_snapshot(
                        track, &mut stats,
                    ))?;
                    return RtcStatsSnapshot::from_ptr(stats, &self._sdk)?.entries();
                }
            }
            Err(invalid("subscribed remote track not found"))
        }
    }
    pub fn remote_data_tracks(&self) -> Result<Vec<RemoteDataTrack>> {
        let mut raw = ptr::null_mut();
        check(unsafe { ffi::lk_room_create_remote_data_track_snapshot(self.as_raw(), &mut raw) })?;
        let _owner = DataTrackList::from_ptr(raw, &self._sdk)?;
        let mut result = Vec::new();
        for i in 0..unsafe { ffi::lk_remote_data_track_list_count(raw) } {
            let mut p = ptr::null();
            check(unsafe { ffi::lk_remote_data_track_list_at(raw, i, &mut p) })?;
            let mut info = native!(ffi::lk_data_track_snapshot_info_init);
            check(unsafe { ffi::lk_remote_data_track_snapshot_info(p, &mut info) })?;
            result.push(RemoteDataTrack {
                participant_identity: read_string(|b, n| unsafe {
                    ffi::lk_remote_data_track_snapshot_publisher_identity(p, b, n)
                })?,
                sid: read_string(|b, n| unsafe {
                    ffi::lk_remote_data_track_snapshot_sid(p, b, n)
                })?,
                name: read_string(|b, n| unsafe {
                    ffi::lk_remote_data_track_snapshot_name(p, b, n)
                })?,
                info,
                custom_frame_encoding: read_string(|b, n| unsafe {
                    ffi::lk_remote_data_track_snapshot_custom_frame_encoding(p, b, n)
                })?,
                schema_name: read_string(|b, n| unsafe {
                    ffi::lk_remote_data_track_snapshot_schema_name(p, b, n)
                })?,
                custom_schema_encoding: read_string(|b, n| unsafe {
                    ffi::lk_remote_data_track_snapshot_custom_schema_encoding(p, b, n)
                })?,
            });
        }
        Ok(result)
    }
}
fn stream_info(
    raw: *mut ffi::lk_data_stream_writer_info_snapshot_t,
    sdk: &Sdk,
) -> Result<StreamInfo> {
    let _owner = WriterInfoHandle::from_ptr(raw, sdk)?;
    let mut info = native!(ffi::lk_data_stream_writer_info_init);
    check(unsafe { ffi::lk_data_stream_writer_info_snapshot_info(raw, &mut info) })?;
    let mut attributes = Vec::new();
    for i in 0..info.attribute_count {
        attributes.push((
            read_string(|b, n| unsafe {
                ffi::lk_data_stream_writer_info_snapshot_attribute_key(raw, i, b, n)
            })?,
            read_string(|b, n| unsafe {
                ffi::lk_data_stream_writer_info_snapshot_attribute_value(raw, i, b, n)
            })?,
        ));
    }
    let mut attached_stream_ids = Vec::new();
    for i in 0..info.attached_stream_id_count {
        attached_stream_ids.push(read_string(|b, n| unsafe {
            ffi::lk_data_stream_writer_info_snapshot_attached_stream_id(raw, i, b, n)
        })?);
    }
    Ok(StreamInfo {
        kind: info.kind,
        total_size: (info.has_total_size != 0).then_some(info.total_size),
        timestamp: info.timestamp,
        stream_id: read_string(|b, n| unsafe {
            ffi::lk_data_stream_writer_info_snapshot_stream_id(raw, b, n)
        })?,
        mime_type: read_string(|b, n| unsafe {
            ffi::lk_data_stream_writer_info_snapshot_mime_type(raw, b, n)
        })?,
        topic: read_string(|b, n| unsafe {
            ffi::lk_data_stream_writer_info_snapshot_topic(raw, b, n)
        })?,
        participant_identity: read_string(|b, n| unsafe {
            ffi::lk_data_stream_writer_info_snapshot_participant_identity(raw, b, n)
        })?,
        name: read_string(|b, n| unsafe {
            ffi::lk_data_stream_writer_info_snapshot_name(raw, b, n)
        })?,
        reply_to_stream_id: read_string(|b, n| unsafe {
            ffi::lk_data_stream_writer_info_snapshot_reply_to_stream_id(raw, b, n)
        })?,
        attributes,
        attached_stream_ids,
    })
}
impl TextWriter {
    pub fn info(&self) -> Result<StreamInfo> {
        let mut raw = ptr::null_mut();
        check(unsafe { ffi::lk_text_stream_writer_create_info_snapshot(self.as_raw(), &mut raw) })?;
        stream_info(raw, self.sdk())
    }
}
impl ByteWriter {
    pub fn info(&self) -> Result<StreamInfo> {
        let mut raw = ptr::null_mut();
        check(unsafe { ffi::lk_byte_stream_writer_create_info_snapshot(self.as_raw(), &mut raw) })?;
        stream_info(raw, self.sdk())
    }
}
