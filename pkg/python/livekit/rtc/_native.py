"""The small, versioned part of the livekit-client-cpp C ABI used by this package."""

from __future__ import annotations

import ctypes as C
import os
import threading
from pathlib import Path


class LiveKitError(RuntimeError):
    def __init__(self, message: str, code: int = 0):
        super().__init__(message)
        self.code = code


class ParticipantInfo(C.Structure):
    _fields_ = [
        ("sid", C.c_char_p), ("identity", C.c_char_p),
        ("name", C.c_char_p), ("metadata", C.c_char_p),
        ("audio_level", C.c_float), ("connection_quality", C.c_int),
        ("is_speaking", C.c_int), ("is_local", C.c_int),
    ]


class TrackPublicationInfo(C.Structure):
    _fields_ = [
        ("sid", C.c_char_p), ("name", C.c_char_p), ("mime_type", C.c_char_p),
        ("kind", C.c_int), ("source", C.c_int),
        ("width", C.c_uint32), ("height", C.c_uint32),
        ("is_muted", C.c_int), ("is_simulcasted", C.c_int),
        ("subscription_allowed", C.c_int),
    ]


class RemoteTrackPublicationInfo(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("kind", C.c_int), ("source", C.c_int),
        ("width", C.c_uint32), ("height", C.c_uint32),
        ("is_muted", C.c_int), ("is_simulcasted", C.c_int),
        ("subscription_allowed", C.c_int), ("subscription_status", C.c_int),
        ("has_subscription_error", C.c_int), ("subscription_error", C.c_int),
        ("has_subscribed_track", C.c_int), ("encryption", C.c_int),
    ]


class RemoteTrackSettings(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("enabled", C.c_int),
        ("has_video_quality", C.c_int), ("video_quality", C.c_int),
        ("video_width", C.c_uint32), ("video_height", C.c_uint32),
        ("video_fps", C.c_uint32), ("priority", C.c_uint32),
    ]


class MediaDeviceInfo(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("kind", C.c_int),
        ("is_default", C.c_int),
    ]


class ScreenSourceInfo(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("kind", C.c_int),
        ("x", C.c_int32), ("y", C.c_int32),
        ("width", C.c_uint32), ("height", C.c_uint32),
    ]


class DataReceived(C.Structure):
    _fields_ = [
        ("data", C.c_void_p), ("data_size", C.c_size_t),
        ("topic", C.c_char_p), ("participant_identity", C.c_char_p),
        ("reliable", C.c_int),
    ]


class ChatMessage(C.Structure):
    _fields_ = [
        ("id", C.c_char_p), ("timestamp", C.c_int64),
        ("has_edit_timestamp", C.c_int), ("edit_timestamp", C.c_int64),
        ("message", C.c_char_p), ("deleted", C.c_int),
        ("generated", C.c_int), ("participant_identity", C.c_char_p),
    ]


class Attribute(C.Structure):
    _fields_ = [("key", C.c_char_p), ("value", C.c_char_p)]


class TokenSourceFetchOptionsRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("room_name", C.c_char_p),
        ("participant_name", C.c_char_p), ("participant_identity", C.c_char_p),
        ("participant_metadata", C.c_char_p),
        ("participant_attributes", C.POINTER(Attribute)),
        ("participant_attribute_count", C.c_size_t),
        ("agent_name", C.c_char_p), ("agent_metadata", C.c_char_p),
        ("deployment", C.c_char_p),
    ]


class TokenSourceResponseRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("server_url", C.c_char_p),
        ("participant_token", C.c_char_p),
    ]


TOKEN_SOURCE_CALLBACK = C.CFUNCTYPE(
    C.c_int, C.c_void_p, C.POINTER(TokenSourceFetchOptionsRaw),
    C.c_int, C.POINTER(TokenSourceResponseRaw),
)


class AudioPlaybackStatsRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("queued_frames", C.c_uint64),
        ("played_frames", C.c_uint64), ("dropped_frames", C.c_uint64),
        ("underrun_frames", C.c_uint64), ("buffered_duration_ms", C.c_uint32),
        ("device_latency_ms", C.c_uint32), ("estimated_delay_ms", C.c_uint32),
    ]


class AudioSourceOptionsRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("sample_rate", C.c_uint32),
        ("num_channels", C.c_uint32), ("queue_size_ms", C.c_uint32),
        ("echo_cancellation", C.c_int), ("auto_gain_control", C.c_int),
        ("noise_suppression", C.c_int),
    ]


class MicrophoneCaptureOptionsRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("device_id", C.c_char_p),
        ("queue_size_ms", C.c_uint32), ("echo_cancellation", C.c_int),
        ("auto_gain_control", C.c_int), ("noise_suppression", C.c_int),
    ]


class SystemAudioCaptureOptionsRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("device_id", C.c_char_p),
        ("queue_size_ms", C.c_uint32),
    ]


class MicrophoneProcessingStatsRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t),
        ("capture_frames_processed", C.c_uint64),
        ("render_frames_processed", C.c_uint64),
        ("capture_processing_errors", C.c_uint64),
        ("render_processing_errors", C.c_uint64),
        ("frames_dropped", C.c_uint64),
        ("echo_cancellation_enabled", C.c_int),
        ("echo_return_loss_available", C.c_int),
        ("echo_return_loss_db", C.c_double),
        ("echo_return_loss_enhancement_available", C.c_int),
        ("echo_return_loss_enhancement_db", C.c_double),
        ("residual_echo_likelihood_available", C.c_int),
        ("residual_echo_likelihood", C.c_double),
        ("residual_echo_likelihood_recent_max_available", C.c_int),
        ("residual_echo_likelihood_recent_max", C.c_double),
        ("delay_median_available", C.c_int), ("delay_median_ms", C.c_int32),
        ("delay_standard_deviation_available", C.c_int),
        ("delay_standard_deviation_ms", C.c_int32),
        ("delay_available", C.c_int), ("delay_ms", C.c_int32),
    ]


class VideoSourceOptionsRaw(C.Structure):
    _fields_ = [("struct_size", C.c_size_t), ("is_screencast", C.c_int)]


class CameraCaptureOptionsRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("device_id", C.c_char_p),
        ("width", C.c_uint32), ("height", C.c_uint32),
        ("frames_per_second", C.c_uint32),
    ]


class ScreenCaptureOptionsRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("source_id", C.c_char_p),
        ("frames_per_second", C.c_uint32), ("include_cursor", C.c_int),
    ]


class MediaStreamOptionsRaw(C.Structure):
    _fields_ = [("struct_size", C.c_size_t), ("capacity", C.c_size_t)]


class AudioFrameRaw(C.Structure):
    _fields_ = [
        ("data", C.c_void_p), ("sample_count", C.c_size_t),
        ("sample_rate", C.c_uint32), ("num_channels", C.c_uint32),
        ("samples_per_channel", C.c_uint32),
    ]


class VideoFrameRaw(C.Structure):
    _fields_ = [
        ("data", C.c_void_p), ("data_size", C.c_size_t),
        ("width", C.c_uint32), ("height", C.c_uint32),
        ("timestamp_us", C.c_int64),
    ]


class VideoFrameMetadataRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("has_user_timestamp_us", C.c_int),
        ("user_timestamp_us", C.c_uint64), ("has_frame_id", C.c_int),
        ("frame_id", C.c_uint32), ("has_user_data", C.c_int),
        ("user_data", C.c_void_p), ("user_data_size", C.c_size_t),
    ]


class VideoEncodingRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("max_bitrate", C.c_uint64),
        ("max_framerate", C.c_float),
    ]


class FrameMetadataFeaturesRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("user_timestamp", C.c_int),
        ("frame_id", C.c_int), ("user_data", C.c_int),
    ]


class TrackPublishOptionsRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("source", C.c_int),
        ("dtx", C.c_int), ("red", C.c_int), ("simulcast", C.c_int),
        ("stream", C.c_char_p), ("video_codec", C.c_int),
        ("scalability_mode", C.c_char_p), ("backup_video_codec_enabled", C.c_int),
        ("backup_video_codec", C.c_int), ("backup_codec_policy", C.c_int),
        ("video_encoding", VideoEncodingRaw),
        ("backup_video_encoding", VideoEncodingRaw),
        ("frame_metadata_features", FrameMetadataFeaturesRaw),
        ("preconnect_buffer", C.c_int), ("has_degradation_preference", C.c_int),
        ("degradation_preference", C.c_int),
    ]


class DataTrackSchemaIdRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("name", C.c_char_p),
        ("encoding", C.c_int), ("custom_encoding", C.c_char_p),
    ]


class DataTrackPublishOptionsRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("name", C.c_char_p),
        ("has_frame_encoding", C.c_int), ("frame_encoding", C.c_int),
        ("custom_frame_encoding", C.c_char_p),
        ("has_schema", C.c_int), ("schema", DataTrackSchemaIdRaw),
    ]


class DataTrackSubscriptionOptionsRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("has_target_fps", C.c_int),
        ("target_fps", C.c_uint32), ("buffer_capacity", C.c_size_t),
        ("max_partial_frames", C.c_size_t),
    ]


class DataTrackSnapshotInfoRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("publisher_handle", C.c_uint16),
        ("uses_e2ee", C.c_int), ("has_frame_encoding", C.c_int),
        ("frame_encoding", C.c_int), ("has_schema", C.c_int),
        ("schema_encoding", C.c_int), ("is_published", C.c_int),
    ]


class DataTrackInfoRaw(C.Structure):
    _fields_ = [
        ("publisher_handle", C.c_uint16), ("sid", C.c_char_p),
        ("name", C.c_char_p), ("uses_e2ee", C.c_int),
        ("has_frame_encoding", C.c_int), ("frame_encoding", C.c_int),
        ("custom_frame_encoding", C.c_char_p), ("has_schema", C.c_int),
        ("schema_name", C.c_char_p), ("schema_encoding", C.c_int),
        ("custom_schema_encoding", C.c_char_p),
    ]


class DataTrackFrameViewRaw(C.Structure):
    _fields_ = [
        ("data", C.c_void_p), ("data_size", C.c_size_t),
        ("has_user_timestamp", C.c_int), ("user_timestamp", C.c_uint64),
    ]


class TextSendOptions(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("topic", C.c_char_p),
        ("destination_identities", C.POINTER(C.c_char_p)),
        ("destination_identity_count", C.c_size_t),
        ("attributes", C.POINTER(Attribute)), ("attribute_count", C.c_size_t),
        ("reply_to_stream_id", C.c_char_p),
        ("attached_stream_ids", C.POINTER(C.c_char_p)),
        ("attached_stream_id_count", C.c_size_t),
        ("chunk_size", C.c_size_t), ("compress", C.c_int),
    ]


class ByteSendOptions(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("topic", C.c_char_p),
        ("mime_type", C.c_char_p), ("name", C.c_char_p),
        ("destination_identities", C.POINTER(C.c_char_p)),
        ("destination_identity_count", C.c_size_t),
        ("attributes", C.POINTER(Attribute)), ("attribute_count", C.c_size_t),
        ("chunk_size", C.c_size_t), ("compress", C.c_int),
    ]


class FileSendOptions(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("topic", C.c_char_p),
        ("mime_type", C.c_char_p),
        ("destination_identities", C.POINTER(C.c_char_p)),
        ("destination_identity_count", C.c_size_t),
        ("chunk_size", C.c_size_t),
        ("attributes", C.POINTER(Attribute)), ("attribute_count", C.c_size_t),
        ("compress", C.c_int),
    ]


class FileReceived(C.Structure):
    _fields_ = [
        ("data", C.c_void_p), ("data_size", C.c_size_t),
        ("stream_id", C.c_char_p), ("name", C.c_char_p),
        ("mime_type", C.c_char_p), ("topic", C.c_char_p),
        ("participant_identity", C.c_char_p),
        ("attributes", C.POINTER(Attribute)), ("attribute_count", C.c_size_t),
        ("timestamp", C.c_int64),
    ]


class TextReceived(C.Structure):
    _fields_ = [
        ("stream_id", C.c_char_p), ("text", C.c_char_p),
        ("topic", C.c_char_p), ("participant_identity", C.c_char_p),
        ("reply_to_stream_id", C.c_char_p), ("timestamp", C.c_int64),
        ("attached_stream_ids", C.POINTER(C.c_char_p)),
        ("attached_stream_id_count", C.c_size_t),
        ("attributes", C.POINTER(Attribute)), ("attribute_count", C.c_size_t),
    ]


class TextStreamEvent(C.Structure):
    _fields_ = [
        ("type", C.c_int), ("stream_id", C.c_char_p),
        ("mime_type", C.c_char_p), ("topic", C.c_char_p),
        ("participant_identity", C.c_char_p),
        ("content", C.c_void_p), ("content_size", C.c_size_t),
        ("chunk_index", C.c_uint64), ("has_total_size", C.c_int),
        ("total_size", C.c_uint64), ("reason", C.c_char_p),
        ("attributes", C.POINTER(Attribute)), ("attribute_count", C.c_size_t),
        ("timestamp", C.c_int64), ("reply_to_stream_id", C.c_char_p),
        ("attached_stream_ids", C.POINTER(C.c_char_p)),
        ("attached_stream_id_count", C.c_size_t),
    ]


class ByteStreamEvent(C.Structure):
    _fields_ = [
        ("type", C.c_int), ("stream_id", C.c_char_p),
        ("name", C.c_char_p), ("mime_type", C.c_char_p),
        ("topic", C.c_char_p), ("participant_identity", C.c_char_p),
        ("content", C.c_void_p), ("content_size", C.c_size_t),
        ("chunk_index", C.c_uint64), ("has_total_size", C.c_int),
        ("total_size", C.c_uint64), ("reason", C.c_char_p),
        ("attributes", C.POINTER(Attribute)), ("attribute_count", C.c_size_t),
        ("timestamp", C.c_int64),
    ]


class DataPublishOptions(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("reliable", C.c_int),
        ("topic", C.c_char_p), ("destination_identities", C.POINTER(C.c_char_p)),
        ("destination_identity_count", C.c_size_t),
    ]


class E2EEOptionsRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("enabled", C.c_int),
        ("shared_key", C.POINTER(C.c_uint8)), ("shared_key_size", C.c_size_t),
        ("ratchet_salt", C.POINTER(C.c_uint8)), ("ratchet_salt_size", C.c_size_t),
        ("unencrypted_magic_bytes", C.POINTER(C.c_uint8)),
        ("unencrypted_magic_bytes_size", C.c_size_t),
        ("ratchet_window_size", C.c_size_t), ("failure_tolerance", C.c_int),
        ("key_ring_size", C.c_size_t), ("key_derivation", C.c_int),
    ]


class EncryptionStateRaw(C.Structure):
    _fields_ = [
        ("track_id", C.c_char_p), ("participant_identity", C.c_char_p),
        ("kind", C.c_int), ("direction", C.c_int), ("enabled", C.c_int),
        ("key_index", C.c_size_t), ("state", C.c_int),
    ]


class FrameCryptorInfoRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("kind", C.c_int),
        ("direction", C.c_int), ("enabled", C.c_int),
        ("key_index", C.c_size_t), ("state", C.c_int),
    ]


class IceServerRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("urls", C.POINTER(C.c_char_p)),
        ("url_count", C.c_size_t), ("username", C.c_char_p),
        ("password", C.c_char_p),
    ]


class ReconnectContextRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("retry_count", C.c_uint32),
        ("elapsed_ms", C.c_uint64), ("reason", C.c_int),
        ("server_url", C.c_char_p),
    ]


RECONNECT_POLICY = C.CFUNCTYPE(
    C.c_int, C.c_void_p, C.POINTER(ReconnectContextRaw), C.POINTER(C.c_uint32)
)


class ParticipantTrackPermissionRaw(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("participant_sid", C.c_char_p),
        ("participant_identity", C.c_char_p), ("allow_all", C.c_int),
        ("allowed_track_sids", C.POINTER(C.c_char_p)),
        ("allowed_track_sid_count", C.c_size_t),
    ]


class RoomConnectOptions(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("auto_subscribe", C.c_int),
        ("adaptive_stream", C.c_int), ("dynacast", C.c_int),
        ("join_retries", C.c_uint32), ("reconnect_timeout_ms", C.c_uint32),
        ("ice_servers", C.POINTER(IceServerRaw)), ("ice_server_count", C.c_size_t),
        ("continual_gathering_policy", C.c_int), ("ice_transport_type", C.c_int),
        ("reconnect_policy", C.c_void_p), ("reconnect_policy_user_data", C.c_void_p),
        ("e2ee_options", C.POINTER(E2EEOptionsRaw)),
    ]


class RpcPerformOptions(C.Structure):
    _fields_ = [
        ("struct_size", C.c_size_t), ("destination_identity", C.c_char_p),
        ("method", C.c_char_p), ("payload", C.c_char_p),
        ("response_timeout_ms", C.c_uint32),
    ]


class RpcInvocationRaw(C.Structure):
    _fields_ = [
        ("request_id", C.c_char_p), ("caller_identity", C.c_char_p),
        ("payload", C.c_char_p), ("response_timeout_ms", C.c_uint32),
    ]


class RpcHandlerResultRaw(C.Structure):
    _fields_ = [
        ("payload", C.c_char_p), ("error_code", C.c_uint32),
        ("error_message", C.c_char_p), ("error_data", C.c_char_p),
    ]


RPC_HANDLER_OUT = C.CFUNCTYPE(
    None, C.c_void_p, C.POINTER(RpcInvocationRaw), C.POINTER(RpcHandlerResultRaw)
)


ROOM_EVENT = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p)
PARTICIPANT_EVENT = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p, C.POINTER(ParticipantInfo))
DATA_EVENT = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p, C.POINTER(DataReceived))
STATE_EVENT = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p, C.c_int)
REASON_EVENT = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p, C.c_int)
METADATA_EVENT = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p, C.c_char_p)
CHAT_EVENT = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p, C.POINTER(ChatMessage))
TRACK_EVENT = C.CFUNCTYPE(
    None, C.c_void_p, C.c_void_p,
    C.POINTER(TrackPublicationInfo), C.POINTER(ParticipantInfo),
)
TRACK_FEEDBACK_EVENT = C.CFUNCTYPE(
    None, C.c_void_p, C.c_void_p,
    C.POINTER(TrackPublicationInfo), C.POINTER(ParticipantInfo), C.c_int,
)
DATA_TRACK_EVENT = C.CFUNCTYPE(
    None, C.c_void_p, C.c_void_p,
    C.POINTER(DataTrackInfoRaw), C.POINTER(ParticipantInfo),
)
DATA_TRACK_FRAME_EVENT = C.CFUNCTYPE(
    None, C.c_void_p, C.c_void_p,
    C.POINTER(DataTrackInfoRaw), C.POINTER(ParticipantInfo),
    C.POINTER(DataTrackFrameViewRaw),
)
AUDIO_FRAME_EVENT = C.CFUNCTYPE(
    None, C.c_void_p, C.c_void_p,
    C.POINTER(TrackPublicationInfo), C.POINTER(ParticipantInfo),
    C.POINTER(AudioFrameRaw),
)
VIDEO_FRAME_EVENT = C.CFUNCTYPE(
    None, C.c_void_p, C.c_void_p,
    C.POINTER(TrackPublicationInfo), C.POINTER(ParticipantInfo),
    C.POINTER(VideoFrameRaw),
)
VIDEO_FRAME_METADATA_EVENT = C.CFUNCTYPE(
    None, C.c_void_p, C.c_void_p,
    C.POINTER(TrackPublicationInfo), C.POINTER(ParticipantInfo),
    C.POINTER(VideoFrameRaw), C.POINTER(VideoFrameMetadataRaw),
)
FILE_EVENT = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p, C.POINTER(FileReceived))
TEXT_EVENT = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p, C.POINTER(TextReceived))
TEXT_STREAM_HANDLER = C.CFUNCTYPE(
    None, C.c_void_p, C.c_void_p, C.POINTER(TextStreamEvent)
)
BYTE_STREAM_HANDLER = C.CFUNCTYPE(
    None, C.c_void_p, C.c_void_p, C.POINTER(ByteStreamEvent)
)
ENCRYPTION_EVENT = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p,
                              C.POINTER(EncryptionStateRaw))

# Keep the complete field order. Native init uses struct_size to version the callback table.
_CALLBACK_NAMES = """on_connected on_disconnected on_participant_connected
on_participant_disconnected on_track_published on_track_unpublished on_track_muted
on_track_unmuted on_track_subscribed on_audio_frame on_video_frame on_data_received
on_file_received on_room_metadata_changed on_connection_quality_changed
on_active_speakers_changed on_local_track_published on_local_track_unpublished
on_text_received on_byte_received on_reconnecting on_reconnected
on_disconnected_with_reason on_track_subscription_permission_changed
on_track_subscription_failed on_track_unsubscribed on_track_stream_state_changed
on_track_subscription_status_changed on_data_channel_buffer_status_changed
on_sip_dtmf_received on_chat_message_received on_transcription_received
on_recording_status_changed on_metrics_received on_connection_state_changed
on_participant_permissions_changed on_local_track_subscribed
on_subscribed_quality_update on_encryption_state_changed
on_participant_metadata_changed on_participant_name_changed
on_participant_attributes_changed on_data_track_published on_data_track_unpublished
on_local_data_track_published on_local_data_track_unpublished on_data_track_frame
on_token_refreshed on_room_updated on_room_sid_changed on_room_moved on_room_eos
on_participants_updated on_video_frame_with_metadata""".split()


class RoomCallbacks(C.Structure):
    _fields_ = [("struct_size", C.c_size_t), ("user_data", C.c_void_p)] + [
        (name, C.c_void_p) for name in _CALLBACK_NAMES
    ]


def utf8(value: str) -> bytes:
    if not isinstance(value, str):
        raise TypeError("expected str")
    return value.encode("utf-8")


def decode(value: bytes | None) -> str:
    return value.decode("utf-8", errors="replace") if value else ""


def _load_library() -> C.CDLL:
    path = os.environ.get("LIVEKIT_CLIENT_CPP_LIBRARY")
    if path:
        try:
            return C.CDLL(str(Path(path).expanduser()))
        except OSError as error:
            raise LiveKitError(f"could not load livekitclient library: {error}") from error
    # Use the platform loader so an installed SDK can live on PATH/LD_LIBRARY_PATH.
    from ctypes.util import find_library

    name = find_library("livekitclient")
    if not name:
        raise LiveKitError(
            "livekitclient shared library was not found; set LIVEKIT_CLIENT_CPP_LIBRARY"
        )
    try:
        return C.CDLL(name)
    except OSError as error:
        raise LiveKitError(f"could not load livekitclient library: {error}") from error


class Native:
    def __init__(self):
        self.lib = _load_library()
        p = C.c_void_p
        s = C.c_size_t
        b = C.c_char_p
        self._bind("lk_init", C.c_int)
        self._bind("lk_version", s, b, s)
        self._bind("lk_last_error", b)
        self._bind("lk_room_create", C.c_int, C.POINTER(p))
        self._bind("lk_room_destroy", None, p)
        self._bind("lk_room_callbacks_init", None, C.POINTER(RoomCallbacks))
        self._bind("lk_room_set_callbacks", C.c_int, p, C.POINTER(RoomCallbacks))
        self._bind("lk_room_connect_with_options", C.c_int, p, b, b, C.POINTER(RoomConnectOptions))
        self._bind("lk_room_connect_with_token_source_and_options", C.c_int, p,
                   TOKEN_SOURCE_CALLBACK, p, C.POINTER(TokenSourceFetchOptionsRaw),
                   C.POINTER(RoomConnectOptions))
        self._bind("lk_room_connect_options_init", None, C.POINTER(RoomConnectOptions))
        self._bind("lk_token_source_fetch_options_init", None,
                   C.POINTER(TokenSourceFetchOptionsRaw))
        self._bind("lk_token_source_response_init", None,
                   C.POINTER(TokenSourceResponseRaw))
        self._bind("lk_audio_playback_stats_init", None, C.POINTER(AudioPlaybackStatsRaw))
        self._bind("lk_ice_server_init", None, C.POINTER(IceServerRaw))
        self._bind("lk_e2ee_options_init", None, C.POINTER(E2EEOptionsRaw))
        self._bind("lk_room_disconnect", C.c_int, p)
        self._bind("lk_room_state", C.c_int, p)
        self._bind("lk_room_is_connected", C.c_int, p)
        self._bind("lk_room_is_recording", C.c_int, p)
        self._bind("lk_room_set_audio_output_device", C.c_int, p, b)
        self._bind("lk_room_audio_output_device", s, p, b, s)
        self._bind("lk_room_set_speaker_volume", C.c_int, p, C.c_float)
        self._bind("lk_room_speaker_volume", C.c_float, p)
        self._bind("lk_room_set_speaker_muted", C.c_int, p, C.c_int)
        self._bind("lk_room_speaker_is_muted", C.c_int, p)
        self._bind("lk_room_audio_playback_stats", C.c_int, p,
                   C.POINTER(AudioPlaybackStatsRaw))
        self._bind("lk_audio_source_options_init", None, C.POINTER(AudioSourceOptionsRaw))
        self._bind("lk_microphone_capture_options_init", None,
                   C.POINTER(MicrophoneCaptureOptionsRaw))
        self._bind("lk_system_audio_capture_options_init", None,
                   C.POINTER(SystemAudioCaptureOptionsRaw))
        self._bind("lk_microphone_processing_stats_init", None,
                   C.POINTER(MicrophoneProcessingStatsRaw))
        self._bind("lk_video_source_options_init", None, C.POINTER(VideoSourceOptionsRaw))
        self._bind("lk_camera_capture_options_init", None, C.POINTER(CameraCaptureOptionsRaw))
        self._bind("lk_screen_capture_options_init", None, C.POINTER(ScreenCaptureOptionsRaw))
        self._bind("lk_track_publish_options_init", None, C.POINTER(TrackPublishOptionsRaw))
        self._bind("lk_audio_source_create", C.c_int, C.POINTER(AudioSourceOptionsRaw), C.POINTER(p))
        self._bind("lk_audio_source_destroy", C.c_int, p)
        self._bind("lk_audio_source_capture_frame", C.c_int, p, p, C.c_uint32)
        self._bind("lk_audio_source_queued_duration_ms", C.c_int, p, C.POINTER(C.c_uint32))
        self._bind("lk_audio_source_clear_queue", C.c_int, p)
        self._bind("lk_audio_source_wait_for_playout", C.c_int, p, C.c_uint32)
        self._bind("lk_audio_source_create_microphone", C.c_int,
                   C.POINTER(MicrophoneCaptureOptionsRaw), C.POINTER(p))
        self._bind("lk_audio_source_create_system_audio", C.c_int,
                   C.POINTER(SystemAudioCaptureOptionsRaw), C.POINTER(p))
        for kind in ("microphone", "system_audio"):
            prefix = "lk_audio_source_" + kind
            self._bind(prefix + "_start", C.c_int, p)
            self._bind(prefix + "_stop", C.c_int, p)
            self._bind(prefix + "_is_capturing", C.c_int, p)
            self._bind(prefix + "_device_id", s, p, b, s)
            self._bind(prefix + "_switch_device", C.c_int, p, b)
        self._bind("lk_audio_source_microphone_set_muted", C.c_int, p, C.c_int)
        self._bind("lk_audio_source_microphone_is_muted", C.c_int, p)
        self._bind("lk_audio_source_microphone_set_volume", C.c_int, p, C.c_float)
        self._bind("lk_audio_source_microphone_volume", C.c_float, p)
        self._bind("lk_audio_source_microphone_set_processing_options", C.c_int, p,
                   C.POINTER(AudioSourceOptionsRaw))
        self._bind("lk_audio_source_microphone_processing_options", C.c_int, p,
                   C.POINTER(AudioSourceOptionsRaw))
        self._bind("lk_audio_source_microphone_processing_stats", C.c_int, p,
                   C.POINTER(MicrophoneProcessingStatsRaw))
        self._bind("lk_video_source_create", C.c_int, C.POINTER(VideoSourceOptionsRaw), C.POINTER(p))
        self._bind("lk_video_source_create_camera", C.c_int,
                   C.POINTER(CameraCaptureOptionsRaw), C.POINTER(p))
        self._bind("lk_video_source_create_screen", C.c_int,
                   C.POINTER(ScreenCaptureOptionsRaw), C.POINTER(p))
        self._bind("lk_video_source_destroy", C.c_int, p)
        self._bind("lk_video_source_dimensions", C.c_int, p,
                   C.POINTER(C.c_uint32), C.POINTER(C.c_uint32))
        self._bind("lk_video_source_capture_i420", C.c_int, p, p, s,
                   C.c_uint32, C.c_uint32, C.c_int64)
        for kind, id_field in (("camera", "device_id"), ("screen", "source_id")):
            prefix = "lk_video_source_" + kind
            self._bind(prefix + "_start", C.c_int, p)
            self._bind(prefix + "_stop", C.c_int, p)
            self._bind(prefix + "_is_capturing", C.c_int, p)
            self._bind(prefix + "_" + id_field, s, p, b, s)
            self._bind(prefix + "_switch_" + ("device" if kind == "camera" else "source"),
                       C.c_int, p, b)
        self._bind("lk_room_create_audio_track", C.c_int, p, b, p, C.POINTER(p))
        self._bind("lk_room_create_video_track", C.c_int, p, b, p, C.POINTER(p))
        self._bind("lk_local_track_publish", C.c_int, p, p, C.POINTER(TrackPublishOptionsRaw))
        self._bind("lk_local_track_unpublish", C.c_int, p, C.c_int)
        self._bind("lk_local_track_set_muted", C.c_int, p, C.c_int)
        self._bind("lk_local_track_destroy", C.c_int, p)
        self._bind("lk_room_republish_all_tracks", C.c_int, p)
        self._bind("lk_media_stream_options_init", None, C.POINTER(MediaStreamOptionsRaw))
        for kind, frame in (("audio", AudioFrameRaw), ("video", VideoFrameRaw)):
            self._bind(f"lk_room_create_{kind}_stream", C.c_int, p, b, b,
                       C.POINTER(MediaStreamOptionsRaw), C.POINTER(p))
            self._bind(f"lk_{kind}_stream_destroy", None, p)
            self._bind(f"lk_{kind}_stream_close", None, p)
            self._bind(f"lk_{kind}_stream_is_closed", C.c_int, p)
            self._bind(f"lk_{kind}_stream_dropped_frames", s, p)
            self._bind(f"lk_{kind}_stream_read_for", C.c_int, p, C.c_uint32, C.POINTER(p))
            self._bind(f"lk_owned_{kind}_frame_destroy", None, p)
            self._bind(f"lk_owned_{kind}_frame_data", C.c_int, p, C.POINTER(frame))
        self._bind("lk_owned_video_frame_metadata", C.c_int, p,
                   C.POINTER(VideoFrameMetadataRaw))
        self._bind("lk_data_track_schema_id_init", None, C.POINTER(DataTrackSchemaIdRaw))
        self._bind("lk_data_track_publish_options_init", None,
                   C.POINTER(DataTrackPublishOptionsRaw))
        self._bind("lk_data_track_subscription_options_init", None,
                   C.POINTER(DataTrackSubscriptionOptionsRaw))
        self._bind("lk_data_track_snapshot_info_init", None,
                   C.POINTER(DataTrackSnapshotInfoRaw))
        self._bind("lk_room_store_data_track_schema", C.c_int, p,
                   C.POINTER(DataTrackSchemaIdRaw), p, s)
        self._bind("lk_room_get_data_track_schema", C.c_int, p, b,
                   C.POINTER(DataTrackSchemaIdRaw), C.POINTER(p))
        self._bind("lk_data_track_schema_destroy", None, p)
        self._bind("lk_data_track_schema_name", s, p, b, s)
        self._bind("lk_data_track_schema_encoding", C.c_int, p)
        self._bind("lk_data_track_schema_custom_encoding", s, p, b, s)
        self._bind("lk_data_track_schema_definition", s, p, p, s)
        self._bind("lk_room_publish_data_track", C.c_int, p,
                   C.POINTER(DataTrackPublishOptionsRaw), C.POINTER(p))
        self._bind("lk_local_data_track_try_push", C.c_int, p, p, s, C.c_int, C.c_uint64)
        self._bind("lk_local_data_track_unpublish", C.c_int, p)
        self._bind("lk_local_data_track_destroy", C.c_int, p)
        self._bind("lk_local_data_track_is_published", C.c_int, p)
        self._bind("lk_local_data_track_sid", s, p, b, s)
        self._bind("lk_local_data_track_name", s, p, b, s)
        self._bind("lk_room_create_remote_data_track_snapshot", C.c_int, p, C.POINTER(p))
        self._bind("lk_remote_data_track_list_destroy", None, p)
        self._bind("lk_remote_data_track_list_count", s, p)
        self._bind("lk_remote_data_track_list_at", C.c_int, p, s, C.POINTER(p))
        self._bind("lk_remote_data_track_snapshot_info", C.c_int, p,
                   C.POINTER(DataTrackSnapshotInfoRaw))
        for suffix in ("publisher_identity", "sid", "name", "custom_frame_encoding",
                       "schema_name", "custom_schema_encoding"):
            self._bind("lk_remote_data_track_snapshot_" + suffix, s, p, b, s)
        self._bind("lk_room_subscribe_data_track", C.c_int, p, b, b,
                   C.POINTER(DataTrackSubscriptionOptionsRaw), C.POINTER(p))
        self._bind("lk_room_update_data_track_subscription_options", C.c_int, p, b, b,
                   C.POINTER(DataTrackSubscriptionOptionsRaw))
        self._bind("lk_data_track_reader_destroy", None, p)
        self._bind("lk_data_track_reader_close", None, p)
        self._bind("lk_data_track_reader_is_closed", C.c_int, p)
        self._bind("lk_data_track_reader_dropped_frames", s, p)
        self._bind("lk_data_track_reader_read_for", C.c_int, p, C.c_uint32, C.POINTER(p))
        self._bind("lk_data_track_frame_destroy", None, p)
        self._bind("lk_data_track_frame_data", s, p, p, s)
        self._bind("lk_data_track_frame_has_user_timestamp", C.c_int, p)
        self._bind("lk_data_track_frame_user_timestamp", C.c_uint64, p)
        self._bind("lk_room_disconnect_reason", C.c_int, p)
        self._bind("lk_room_e2ee_is_configured", C.c_int, p)
        self._bind("lk_room_e2ee_is_enabled", C.c_int, p)
        self._bind("lk_room_e2ee_set_enabled", C.c_int, p, C.c_int)
        self._bind("lk_room_e2ee_set_shared_key", C.c_int, p, p, s, s)
        self._bind("lk_room_e2ee_export_shared_key", s, p, s, p, s)
        self._bind("lk_room_e2ee_ratchet_shared_key", C.c_int, p, s)
        self._bind("lk_room_e2ee_remove_shared_key", C.c_int, p, s)
        self._bind("lk_room_e2ee_set_participant_key", C.c_int, p, b, p, s, s)
        self._bind("lk_room_e2ee_export_participant_key", s, p, b, s, p, s)
        self._bind("lk_room_e2ee_ratchet_participant_key", C.c_int, p, b, s)
        self._bind("lk_room_e2ee_remove_participant_key", C.c_int, p, b, s)
        self._bind("lk_room_e2ee_remove_participant_keys", C.c_int, p, b)
        self._bind("lk_room_e2ee_clear_keys", C.c_int, p)
        self._bind("lk_room_e2ee_data_key_index", s, p)
        self._bind("lk_room_e2ee_set_data_key_index", C.c_int, p, s)
        self._bind("lk_room_e2ee_set_frame_cryptor_enabled", C.c_int, p, b,
                   C.c_int, C.c_int)
        self._bind("lk_room_e2ee_set_frame_cryptor_key_index", C.c_int, p, b,
                   C.c_int, s)
        self._bind("lk_room_e2ee_set_participant_enabled", C.c_int, p, b,
                   C.c_int, C.POINTER(s))
        self._bind("lk_frame_cryptor_list_create", C.c_int, p, C.POINTER(p))
        self._bind("lk_frame_cryptor_list_destroy", None, p)
        self._bind("lk_frame_cryptor_list_count", s, p)
        self._bind("lk_frame_cryptor_list_info", C.c_int, p, s,
                   C.POINTER(FrameCryptorInfoRaw))
        self._bind("lk_frame_cryptor_list_track_id", s, p, s, b, s)
        self._bind("lk_frame_cryptor_list_participant_identity", s, p, s, b, s)
        for name in ("lk_room_sid", "lk_room_name", "lk_room_metadata",
                     "lk_local_participant_sid", "lk_local_participant_identity",
                     "lk_local_participant_name", "lk_local_participant_metadata"):
            self._bind(name, s, p, b, s)
        self._bind("lk_local_participant_set_name", C.c_int, p, b)
        self._bind("lk_local_participant_set_metadata", C.c_int, p, b)
        self._bind("lk_local_participant_set_attributes", C.c_int, p,
                   C.POINTER(Attribute), s)
        self._bind("lk_room_publish_data", C.c_int, p, p, s, C.POINTER(DataPublishOptions))
        self._bind("lk_data_publish_options_init", None, C.POINTER(DataPublishOptions))
        self._bind("lk_room_publish_dtmf", C.c_int, p, C.c_uint32, b)
        self._bind("lk_room_send_chat_message", C.c_int, p, b, b, s,
                   C.POINTER(C.c_int64))
        self._bind("lk_room_edit_chat_message", C.c_int, p, b, C.c_int64, b)
        self._bind("lk_room_send_text", C.c_int, p, b, C.POINTER(TextSendOptions))
        self._bind("lk_text_send_options_init", None, C.POINTER(TextSendOptions))
        self._bind("lk_room_send_bytes", C.c_int, p, p, s, C.POINTER(ByteSendOptions))
        self._bind("lk_byte_send_options_init", None, C.POINTER(ByteSendOptions))
        self._bind("lk_room_send_file", C.c_int, p, b, C.POINTER(FileSendOptions))
        self._bind("lk_file_send_options_init", None, C.POINTER(FileSendOptions))
        self._bind("lk_room_register_text_stream_handler", C.c_int, p, b,
                   TEXT_STREAM_HANDLER, p)
        self._bind("lk_room_unregister_text_stream_handler", C.c_int, p, b)
        self._bind("lk_room_register_byte_stream_handler", C.c_int, p, b,
                   BYTE_STREAM_HANDLER, p)
        self._bind("lk_room_unregister_byte_stream_handler", C.c_int, p, b)
        self._bind("lk_room_perform_rpc", C.c_int, p, C.POINTER(RpcPerformOptions), C.POINTER(p))
        self._bind("lk_room_register_rpc_method_out", C.c_int, p, b, RPC_HANDLER_OUT, p)
        self._bind("lk_room_unregister_rpc_method", C.c_int, p, b)
        self._bind("lk_rpc_perform_options_init", None, C.POINTER(RpcPerformOptions))
        self._bind("lk_rpc_result_destroy", None, p)
        self._bind("lk_rpc_result_ok", C.c_int, p)
        self._bind("lk_rpc_result_payload", s, p, b, s)
        self._bind("lk_rpc_result_error_code", C.c_uint32, p)
        self._bind("lk_rpc_result_error_message", s, p, b, s)
        self._bind("lk_rpc_result_error_data", s, p, b, s)
        self._bind("lk_room_create_remote_participant_snapshot", C.c_int, p, C.POINTER(p))
        self._bind("lk_remote_participant_list_destroy", None, p)
        self._bind("lk_remote_participant_list_count", s, p)
        self._bind("lk_remote_participant_list_at", C.c_int, p, s, C.POINTER(p))
        for name in ("lk_remote_participant_snapshot_sid",
                     "lk_remote_participant_snapshot_identity",
                     "lk_remote_participant_snapshot_name",
                     "lk_remote_participant_snapshot_metadata"):
            self._bind(name, s, p, b, s)
        self._bind("lk_remote_participant_snapshot_attribute_count", s, p)
        for name in ("lk_remote_participant_snapshot_attribute_key",
                     "lk_remote_participant_snapshot_attribute_value"):
            self._bind(name, s, p, s, b, s)
        self._bind("lk_remote_participant_snapshot_publication_count", s, p)
        self._bind("lk_remote_participant_snapshot_publication_at", C.c_int,
                   p, s, C.POINTER(p))
        self._bind("lk_remote_track_publication_snapshot_info", C.c_int,
                   p, C.POINTER(RemoteTrackPublicationInfo))
        for name in ("lk_remote_track_publication_snapshot_sid",
                     "lk_remote_track_publication_snapshot_name",
                     "lk_remote_track_publication_snapshot_mime_type"):
            self._bind(name, s, p, b, s)
        self._bind("lk_remote_track_publication_snapshot_info_init", None,
                   C.POINTER(RemoteTrackPublicationInfo))
        self._bind("lk_remote_track_settings_init", None, C.POINTER(RemoteTrackSettings))
        self._bind("lk_room_set_remote_track_subscribed", C.c_int, p, b, b, C.c_int)
        self._bind("lk_room_update_remote_track_settings", C.c_int, p, b, b,
                   C.POINTER(RemoteTrackSettings))
        self._bind("lk_participant_track_permission_init", None,
                   C.POINTER(ParticipantTrackPermissionRaw))
        self._bind("lk_room_set_track_subscription_permissions", C.c_int, p,
                   C.c_int, C.POINTER(ParticipantTrackPermissionRaw), s)
        for prefix, info in (("lk_media_device_list", MediaDeviceInfo),
                             ("lk_screen_source_list", ScreenSourceInfo)):
            self._bind(prefix + "_create", C.c_int, C.POINTER(p))
            self._bind(prefix + "_destroy", None, p)
            self._bind(prefix + "_count", s, p)
            self._bind(prefix + "_info", C.c_int, p, s, C.POINTER(info))
            self._bind(prefix + "_id", s, p, s, b, s)
            self._bind(prefix + "_label", s, p, s, b, s)
        self.check(self.lib.lk_init())

    def _bind(self, name, result, *args):
        try:
            fn = getattr(self.lib, name)
        except AttributeError as error:
            raise LiveKitError(
                f"native library is missing {name}; use a matching livekit-client-cpp build"
            ) from error
        fn.restype = result
        fn.argtypes = list(args)

    def check(self, status: int):
        if status:
            message = decode(self.lib.lk_last_error()) or f"LiveKit status {status}"
            raise LiveKitError(message, status)

    def string(self, name: str, room: C.c_void_p) -> str:
        fn = getattr(self.lib, name)
        size = fn(room, None, 0)
        if not size:
            return ""
        value = C.create_string_buffer(size)
        fn(room, value, size)
        return decode(value.value)


_native: Native | None = None
_native_lock = threading.Lock()


def native() -> Native:
    global _native
    with _native_lock:
        if _native is None:
            _native = Native()
    return _native
