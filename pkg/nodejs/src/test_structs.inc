// Generated: available only in LIVEKIT_NODE_TESTING builds.
if (type == "CErrorInfo") {
	lk_error_info_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CLogRecord") {
	lk_log_record_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CLogOptions") {
	lk_log_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CTraceRecord") {
	lk_trace_record_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CTraceOptions") {
	lk_trace_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CParticipantInfo") {
	lk_participant_info_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CMediaDeviceInfo") {
	lk_media_device_info_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CAudioPlaybackStats") {
	lk_audio_playback_stats_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CScreenSourceInfo") {
	lk_screen_source_info_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CParticipantPermissions") {
	lk_participant_permissions_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CTrackPublicationInfo") {
	lk_track_publication_info_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CRemoteParticipantSnapshotInfo") {
	lk_remote_participant_snapshot_info_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CLocalParticipantSnapshotInfo") {
	lk_local_participant_snapshot_info_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CDataStreamWriterInfo") {
	lk_data_stream_writer_info_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CRemoteTrackPublicationSnapshotInfo") {
	lk_remote_track_publication_snapshot_info_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CRemoteTrackSnapshotInfo") {
	lk_remote_track_snapshot_info_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CRtcTrackStats") {
	lk_rtc_track_stats_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CSubscribedQuality") {
	lk_subscribed_quality_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CSubscribedCodec") {
	lk_subscribed_codec_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CSubscribedQualityUpdate") {
	lk_subscribed_quality_update_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CEncryptionState") {
	lk_encryption_state_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CFrameCryptorInfo") {
	lk_frame_cryptor_info_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CAudioFrame") {
	lk_audio_frame_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CVideoFrame") {
	lk_video_frame_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CVideoPlane") {
	lk_video_plane_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CVideoFrameMetadata") {
	lk_video_frame_metadata_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CVideoFrameInput") {
	lk_video_frame_input_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CDataReceived") {
	lk_data_received_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CDataTrackInfo") {
	lk_data_track_info_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CDataTrackFrameView") {
	lk_data_track_frame_view_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CDataTrackSnapshotInfo") {
	lk_data_track_snapshot_info_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CSipDtmf") {
	lk_sip_dtmf_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CChatMessage") {
	lk_chat_message_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CTranscriptionSegment") {
	lk_transcription_segment_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CTranscriptionReceived") {
	lk_transcription_received_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CMetricTimestamp") {
	lk_metric_timestamp_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CMetricSample") {
	lk_metric_sample_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CTimeSeriesMetric") {
	lk_time_series_metric_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CEventMetric") {
	lk_event_metric_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CMetricsReceived") {
	lk_metrics_received_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CAttribute") {
	lk_attribute_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CTokenSourceFetchOptions") {
	lk_token_source_fetch_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CTokenSourceResponse") {
	lk_token_source_response_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CRoomSnapshot") {
	lk_room_snapshot_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CFileReceived") {
	lk_file_received_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CTextReceived") {
	lk_text_received_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CTextStreamEvent") {
	lk_text_stream_event_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CByteStreamEvent") {
	lk_byte_stream_event_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CDataChannelBufferStatus") {
	lk_data_channel_buffer_status_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CDataStreamCompletion") {
	lk_data_stream_completion_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CRpcInvocation") {
	lk_rpc_invocation_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CRpcHandlerResult") {
	lk_rpc_handler_result_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CE2EeOptions") {
	lk_e2ee_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CIceServer") {
	lk_ice_server_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CReconnectContext") {
	lk_reconnect_context_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CRoomConnectOptions") {
	lk_room_connect_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CAudioSourceOptions") {
	lk_audio_source_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CMicrophoneCaptureOptions") {
	lk_microphone_capture_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CSystemAudioCaptureOptions") {
	lk_system_audio_capture_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CMicrophoneProcessingStats") {
	lk_microphone_processing_stats_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CVideoSourceOptions") {
	lk_video_source_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CCameraCaptureOptions") {
	lk_camera_capture_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CScreenCaptureOptions") {
	lk_screen_capture_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CVideoEncoding") {
	lk_video_encoding_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CFrameMetadataFeatures") {
	lk_frame_metadata_features_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CTrackPublishOptions") {
	lk_track_publish_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CDataPublishOptions") {
	lk_data_publish_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CDataTrackSchemaId") {
	lk_data_track_schema_id_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CDataTrackPublishOptions") {
	lk_data_track_publish_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CDataTrackSubscriptionOptions") {
	lk_data_track_subscription_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CMediaStreamOptions") {
	lk_media_stream_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CTrackRecorderOptions") {
	lk_track_recorder_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CTrackRecorderStats") {
	lk_track_recorder_stats_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CFileSendOptions") {
	lk_file_send_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CTextSendOptions") {
	lk_text_send_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CByteSendOptions") {
	lk_byte_send_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CStreamTextOptions") {
	lk_stream_text_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CStreamBytesOptions") {
	lk_stream_bytes_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CRpcPerformOptions") {
	lk_rpc_perform_options_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CParticipantTrackPermission") {
	lk_participant_track_permission_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
if (type == "CRemoteTrackSettings") {
	lk_remote_track_settings_t value{};
	Read(env, Get(env, args, "value"), value, arena);
	return Pack(value);
}
