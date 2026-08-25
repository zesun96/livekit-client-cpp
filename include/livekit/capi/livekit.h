#pragma once

#ifndef LKC_CAPI_LIVEKIT_H
#define LKC_CAPI_LIVEKIT_H

/*
 * Stable C ABI for the LiveKit C++ client.
 *
 * Object pointers are opaque handles owned by the caller. Destroy tracks before their sources.
 * A published track must remain alive until its room is disconnected. Callback data, strings,
 * and frame buffers are borrowed and remain valid only for the duration of that callback. A room
 * must not be destroyed from one of its own callbacks.
 *
 * String getters return the required size including the trailing NUL. Pass NULL and zero to query
 * the size. All functions catch C++ exceptions; details for a failure on the current thread are
 * available through lk_last_error().
 */

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(LKC_SHARED)
#if defined(LKC_BUILDING_LIBRARY)
#define LKC_API __declspec(dllexport)
#else
#define LKC_API __declspec(dllimport)
#endif
#else
#define LKC_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lk_room lk_room_t;
typedef struct lk_audio_source lk_audio_source_t;
typedef struct lk_video_source lk_video_source_t;
typedef struct lk_local_track lk_local_track_t;
typedef struct lk_rpc_result lk_rpc_result_t;
typedef struct lk_text_stream_writer lk_text_stream_writer_t;
typedef struct lk_byte_stream_writer lk_byte_stream_writer_t;
typedef struct lk_remote_participant_list lk_remote_participant_list_t;
typedef struct lk_remote_participant_snapshot lk_remote_participant_snapshot_t;
typedef struct lk_remote_track_publication_snapshot lk_remote_track_publication_snapshot_t;
typedef struct lk_remote_track_snapshot lk_remote_track_snapshot_t;
typedef struct lk_media_device_list lk_media_device_list_t;
typedef struct lk_screen_source_list lk_screen_source_list_t;
typedef struct lk_frame_cryptor_list lk_frame_cryptor_list_t;
typedef struct lk_local_data_track lk_local_data_track_t;
typedef struct lk_data_track_reader lk_data_track_reader_t;
typedef struct lk_data_track_frame lk_data_track_frame_t;
typedef struct lk_data_track_schema lk_data_track_schema_t;

typedef enum lk_status {
	LK_STATUS_OK = 0,
	LK_STATUS_INVALID_ARGUMENT = 1,
	LK_STATUS_INVALID_STATE = 2,
	LK_STATUS_OPERATION_FAILED = 3,
	LK_STATUS_EXCEPTION = 4
} lk_status_t;

typedef enum lk_log_level {
	LK_LOG_LEVEL_TRACE = 0,
	LK_LOG_LEVEL_DEBUG = 1,
	LK_LOG_LEVEL_INFO = 2,
	LK_LOG_LEVEL_WARNING = 3,
	LK_LOG_LEVEL_ERROR = 4,
	LK_LOG_LEVEL_OFF = 5
} lk_log_level_t;

typedef enum lk_log_source {
	LK_LOG_SOURCE_LIVEKIT = 0,
	LK_LOG_SOURCE_WEBRTC = 1,
	LK_LOG_SOURCE_WEBSOCKET = 2
} lk_log_source_t;

/* Strings are borrowed and remain valid only for the duration of the callback. */
typedef struct lk_log_record {
	size_t struct_size;
	lk_log_level_t level;
	lk_log_source_t source;
	const char* message;
	const char* file;
	int32_t line;
} lk_log_record_t;

typedef struct lk_log_options {
	size_t struct_size;
	lk_log_level_t livekit_level;
	lk_log_level_t webrtc_level;
	lk_log_level_t websocket_level;
} lk_log_options_t;

/*
 * Invoked synchronously from SDK, WebRTC, and WebSocket threads. The callback must be thread-safe
 * and return quickly. Do not call a logging configuration function, lk_init(), or lk_shutdown()
 * from this callback. Exceptions must not cross the C ABI boundary.
 */
typedef void (*lk_log_callback)(void* user_data, const lk_log_record_t* record);

typedef enum lk_room_state {
	LK_ROOM_STATE_CONNECTING = 0,
	LK_ROOM_STATE_CONNECTED = 1,
	LK_ROOM_STATE_DISCONNECTING = 2,
	LK_ROOM_STATE_DISCONNECTED = 3,
	LK_ROOM_STATE_FAILED = 4,
	LK_ROOM_STATE_RECONNECTING = 5
} lk_room_state_t;

typedef enum lk_disconnect_reason {
	LK_DISCONNECT_REASON_UNKNOWN = 0,
	LK_DISCONNECT_REASON_CLIENT_INITIATED = 1,
	LK_DISCONNECT_REASON_DUPLICATE_IDENTITY = 2,
	LK_DISCONNECT_REASON_SERVER_SHUTDOWN = 3,
	LK_DISCONNECT_REASON_PARTICIPANT_REMOVED = 4,
	LK_DISCONNECT_REASON_ROOM_DELETED = 5,
	LK_DISCONNECT_REASON_STATE_MISMATCH = 6,
	LK_DISCONNECT_REASON_JOIN_FAILURE = 7,
	LK_DISCONNECT_REASON_MIGRATION = 8,
	LK_DISCONNECT_REASON_SIGNAL_CLOSE = 9,
	LK_DISCONNECT_REASON_ROOM_CLOSED = 10,
	LK_DISCONNECT_REASON_USER_UNAVAILABLE = 11,
	LK_DISCONNECT_REASON_USER_REJECTED = 12,
	LK_DISCONNECT_REASON_SIP_TRUNK_FAILURE = 13,
	LK_DISCONNECT_REASON_CONNECTION_TIMEOUT = 14,
	LK_DISCONNECT_REASON_MEDIA_FAILURE = 15,
	LK_DISCONNECT_REASON_AGENT_ERROR = 16
} lk_disconnect_reason_t;

typedef enum lk_track_kind {
	LK_TRACK_KIND_UNKNOWN = 0,
	LK_TRACK_KIND_AUDIO = 1,
	LK_TRACK_KIND_VIDEO = 2
} lk_track_kind_t;

typedef enum lk_media_device_kind {
	LK_MEDIA_DEVICE_KIND_AUDIO_INPUT = 0,
	LK_MEDIA_DEVICE_KIND_AUDIO_OUTPUT = 1,
	LK_MEDIA_DEVICE_KIND_VIDEO_INPUT = 2
} lk_media_device_kind_t;

typedef enum lk_screen_source_kind {
	LK_SCREEN_SOURCE_KIND_MONITOR = 0,
	LK_SCREEN_SOURCE_KIND_WINDOW = 1
} lk_screen_source_kind_t;

typedef enum lk_track_source {
	LK_TRACK_SOURCE_UNKNOWN = 0,
	LK_TRACK_SOURCE_CAMERA = 1,
	LK_TRACK_SOURCE_MICROPHONE = 2,
	LK_TRACK_SOURCE_SCREEN_SHARE = 3,
	LK_TRACK_SOURCE_SCREEN_SHARE_AUDIO = 4
} lk_track_source_t;

typedef enum lk_track_stream_state {
	LK_TRACK_STREAM_STATE_UNKNOWN = 0,
	LK_TRACK_STREAM_STATE_ACTIVE = 1,
	LK_TRACK_STREAM_STATE_PAUSED = 2
} lk_track_stream_state_t;

typedef enum lk_track_subscription_status {
	LK_TRACK_SUBSCRIPTION_STATUS_UNSUBSCRIBED = 0,
	LK_TRACK_SUBSCRIPTION_STATUS_DESIRED = 1,
	LK_TRACK_SUBSCRIPTION_STATUS_SUBSCRIBED = 2
} lk_track_subscription_status_t;

typedef enum lk_video_quality {
	LK_VIDEO_QUALITY_LOW = 0,
	LK_VIDEO_QUALITY_MEDIUM = 1,
	LK_VIDEO_QUALITY_HIGH = 2,
	LK_VIDEO_QUALITY_OFF = 3
} lk_video_quality_t;

typedef enum lk_video_codec {
	LK_VIDEO_CODEC_VP8 = 0,
	LK_VIDEO_CODEC_H264 = 1,
	LK_VIDEO_CODEC_VP9 = 2,
	LK_VIDEO_CODEC_AV1 = 3
} lk_video_codec_t;

typedef enum lk_backup_codec_policy {
	LK_BACKUP_CODEC_POLICY_PREFER_REGRESSION = 0,
	LK_BACKUP_CODEC_POLICY_SIMULCAST = 1,
	LK_BACKUP_CODEC_POLICY_REGRESSION = 2
} lk_backup_codec_policy_t;

typedef enum lk_e2ee_key_derivation {
	LK_E2EE_KEY_DERIVATION_PBKDF2_SHA256 = 0,
	LK_E2EE_KEY_DERIVATION_HKDF_SHA256 = 1
} lk_e2ee_key_derivation_t;

typedef enum lk_frame_cryptor_direction {
	LK_FRAME_CRYPTOR_DIRECTION_SENDER = 0,
	LK_FRAME_CRYPTOR_DIRECTION_RECEIVER = 1
} lk_frame_cryptor_direction_t;

typedef enum lk_frame_cryptor_state {
	LK_FRAME_CRYPTOR_STATE_NEW = 0,
	LK_FRAME_CRYPTOR_STATE_OK = 1,
	LK_FRAME_CRYPTOR_STATE_ENCRYPTION_FAILED = 2,
	LK_FRAME_CRYPTOR_STATE_DECRYPTION_FAILED = 3,
	LK_FRAME_CRYPTOR_STATE_MISSING_KEY = 4,
	LK_FRAME_CRYPTOR_STATE_KEY_RATCHETED = 5,
	LK_FRAME_CRYPTOR_STATE_INTERNAL_ERROR = 6
} lk_frame_cryptor_state_t;

typedef enum lk_connection_quality {
	LK_CONNECTION_QUALITY_UNKNOWN = 0,
	LK_CONNECTION_QUALITY_POOR = 1,
	LK_CONNECTION_QUALITY_GOOD = 2,
	LK_CONNECTION_QUALITY_EXCELLENT = 3,
	LK_CONNECTION_QUALITY_LOST = 4
} lk_connection_quality_t;

typedef enum lk_subscription_error {
	LK_SUBSCRIPTION_ERROR_UNKNOWN = 0,
	LK_SUBSCRIPTION_ERROR_CODEC_UNSUPPORTED = 1,
	LK_SUBSCRIPTION_ERROR_TRACK_NOT_FOUND = 2
} lk_subscription_error_t;

typedef enum lk_rpc_error_code {
	LK_RPC_ERROR_UNSUPPORTED_METHOD = 1400,
	LK_RPC_ERROR_RECIPIENT_NOT_FOUND = 1401,
	LK_RPC_ERROR_REQUEST_PAYLOAD_TOO_LARGE = 1402,
	LK_RPC_ERROR_UNSUPPORTED_SERVER = 1403,
	LK_RPC_ERROR_UNSUPPORTED_VERSION = 1404,
	LK_RPC_ERROR_APPLICATION_ERROR = 1500,
	LK_RPC_ERROR_CONNECTION_TIMEOUT = 1501,
	LK_RPC_ERROR_RESPONSE_TIMEOUT = 1502,
	LK_RPC_ERROR_RECIPIENT_DISCONNECTED = 1503,
	LK_RPC_ERROR_RESPONSE_PAYLOAD_TOO_LARGE = 1504,
	LK_RPC_ERROR_SEND_FAILED = 1505
} lk_rpc_error_code_t;

typedef enum lk_data_stream_event_type {
	LK_DATA_STREAM_EVENT_OPEN = 0,
	LK_DATA_STREAM_EVENT_CHUNK = 1,
	LK_DATA_STREAM_EVENT_CLOSED = 2,
	LK_DATA_STREAM_EVENT_FAILED = 3
} lk_data_stream_event_type_t;

typedef enum lk_data_stream_completion_status {
	LK_DATA_STREAM_COMPLETION_COMPLETED = 0,
	LK_DATA_STREAM_COMPLETION_CANCELLED = 1,
	LK_DATA_STREAM_COMPLETION_FAILED = 2
} lk_data_stream_completion_status_t;

typedef enum lk_data_track_frame_encoding {
	LK_DATA_TRACK_FRAME_ENCODING_UNSPECIFIED = 0,
	LK_DATA_TRACK_FRAME_ENCODING_ROS1 = 1,
	LK_DATA_TRACK_FRAME_ENCODING_CDR = 2,
	LK_DATA_TRACK_FRAME_ENCODING_PROTOBUF = 3,
	LK_DATA_TRACK_FRAME_ENCODING_FLATBUFFER = 4,
	LK_DATA_TRACK_FRAME_ENCODING_CBOR = 5,
	LK_DATA_TRACK_FRAME_ENCODING_MSGPACK = 6,
	LK_DATA_TRACK_FRAME_ENCODING_JSON = 7,
	LK_DATA_TRACK_FRAME_ENCODING_CUSTOM = 8
} lk_data_track_frame_encoding_t;

typedef enum lk_data_track_schema_encoding {
	LK_DATA_TRACK_SCHEMA_ENCODING_UNSPECIFIED = 0,
	LK_DATA_TRACK_SCHEMA_ENCODING_PROTOBUF = 1,
	LK_DATA_TRACK_SCHEMA_ENCODING_FLATBUFFER = 2,
	LK_DATA_TRACK_SCHEMA_ENCODING_ROS1_MESSAGE = 3,
	LK_DATA_TRACK_SCHEMA_ENCODING_ROS2_MESSAGE = 4,
	LK_DATA_TRACK_SCHEMA_ENCODING_ROS2_IDL = 5,
	LK_DATA_TRACK_SCHEMA_ENCODING_OMG_IDL = 6,
	LK_DATA_TRACK_SCHEMA_ENCODING_JSON_SCHEMA = 7,
	LK_DATA_TRACK_SCHEMA_ENCODING_CUSTOM = 8
} lk_data_track_schema_encoding_t;

typedef enum lk_data_track_error_code {
	LK_DATA_TRACK_ERROR_NONE = 0,
	LK_DATA_TRACK_ERROR_INVALID_NAME = 1,
	LK_DATA_TRACK_ERROR_INVALID_SCHEMA = 2,
	LK_DATA_TRACK_ERROR_DUPLICATE_NAME = 3,
	LK_DATA_TRACK_ERROR_HANDLE_LIMIT_REACHED = 4,
	LK_DATA_TRACK_ERROR_NOT_ALLOWED = 5,
	LK_DATA_TRACK_ERROR_DISCONNECTED = 6,
	LK_DATA_TRACK_ERROR_TIMEOUT = 7,
	LK_DATA_TRACK_ERROR_UNPUBLISHED = 8,
	LK_DATA_TRACK_ERROR_QUEUE_FULL = 9,
	LK_DATA_TRACK_ERROR_INVALID_FRAME = 10,
	LK_DATA_TRACK_ERROR_PROTOCOL = 11,
	LK_DATA_TRACK_ERROR_SEND_FAILED = 12,
	LK_DATA_TRACK_ERROR_NOT_FOUND = 13,
	LK_DATA_TRACK_ERROR_INVALID_ARGUMENT = 14
} lk_data_track_error_code_t;

typedef enum lk_data_track_read_status {
	LK_DATA_TRACK_READ_FRAME = 0,
	LK_DATA_TRACK_READ_EMPTY = 1,
	LK_DATA_TRACK_READ_CLOSED = 2,
	LK_DATA_TRACK_READ_INVALID_ARGUMENT = 3
} lk_data_track_read_status_t;

typedef struct lk_participant_info {
	const char* sid;
	const char* identity;
	const char* name;
	const char* metadata;
	float audio_level;
	lk_connection_quality_t connection_quality;
	int is_speaking;
	int is_local;
} lk_participant_info_t;

typedef struct lk_media_device_info {
	size_t struct_size;
	lk_media_device_kind_t kind;
	int is_default;
} lk_media_device_info_t;

typedef struct lk_audio_playback_stats {
	size_t struct_size;
	uint64_t queued_frames;
	uint64_t played_frames;
	uint64_t dropped_frames;
	uint64_t underrun_frames;
	uint32_t buffered_duration_ms;
	uint32_t device_latency_ms;
	uint32_t estimated_delay_ms;
} lk_audio_playback_stats_t;

typedef struct lk_screen_source_info {
	size_t struct_size;
	lk_screen_source_kind_t kind;
	int32_t x;
	int32_t y;
	uint32_t width;
	uint32_t height;
} lk_screen_source_info_t;

typedef struct lk_participant_permissions {
	int can_subscribe;
	int can_publish;
	int can_publish_data;
	const lk_track_source_t* can_publish_sources;
	size_t can_publish_source_count;
	int hidden;
	int recorder;
	int can_update_metadata;
	int agent;
	int can_subscribe_metrics;
	int can_manage_agent_session;
} lk_participant_permissions_t;

typedef struct lk_track_publication_info {
	const char* sid;
	const char* name;
	const char* mime_type;
	lk_track_kind_t kind;
	lk_track_source_t source;
	uint32_t width;
	uint32_t height;
	int is_muted;
	int is_simulcasted;
	int subscription_allowed;
} lk_track_publication_info_t;

typedef struct lk_remote_participant_snapshot_info {
	size_t struct_size;
	float audio_level;
	lk_connection_quality_t connection_quality;
	int is_speaking;
} lk_remote_participant_snapshot_info_t;

typedef struct lk_remote_track_publication_snapshot_info {
	size_t struct_size;
	lk_track_kind_t kind;
	lk_track_source_t source;
	uint32_t width;
	uint32_t height;
	int is_muted;
	int is_simulcasted;
	int subscription_allowed;
	lk_track_subscription_status_t subscription_status;
	int has_subscription_error;
	lk_subscription_error_t subscription_error;
	int has_subscribed_track;
} lk_remote_track_publication_snapshot_info_t;

typedef struct lk_remote_track_snapshot_info {
	size_t struct_size;
	lk_track_kind_t kind;
	lk_track_source_t source;
	lk_track_stream_state_t stream_state;
	uint32_t width;
	uint32_t height;
	int enabled;
} lk_remote_track_snapshot_info_t;

typedef struct lk_subscribed_quality {
	lk_video_quality_t quality;
	int enabled;
} lk_subscribed_quality_t;

typedef struct lk_subscribed_codec {
	const char* codec;
	const lk_subscribed_quality_t* qualities;
	size_t quality_count;
} lk_subscribed_codec_t;

typedef struct lk_subscribed_quality_update {
	const char* track_sid;
	const lk_subscribed_quality_t* qualities;
	size_t quality_count;
	const lk_subscribed_codec_t* codecs;
	size_t codec_count;
} lk_subscribed_quality_update_t;

typedef struct lk_encryption_state {
	const char* track_id;
	const char* participant_identity;
	lk_track_kind_t kind;
	lk_frame_cryptor_direction_t direction;
	int enabled;
	size_t key_index;
	lk_frame_cryptor_state_t state;
} lk_encryption_state_t;

typedef struct lk_frame_cryptor_info {
	size_t struct_size;
	lk_track_kind_t kind;
	lk_frame_cryptor_direction_t direction;
	int enabled;
	size_t key_index;
	lk_frame_cryptor_state_t state;
} lk_frame_cryptor_info_t;

typedef struct lk_audio_frame {
	const int16_t* data;
	size_t sample_count;
	uint32_t sample_rate;
	uint32_t num_channels;
	uint32_t samples_per_channel;
} lk_audio_frame_t;

typedef struct lk_video_frame {
	const uint8_t* data;
	size_t data_size;
	uint32_t width;
	uint32_t height;
	int64_t timestamp_us;
} lk_video_frame_t;

typedef struct lk_data_received {
	const uint8_t* data;
	size_t data_size;
	const char* topic;
	const char* participant_identity;
	int reliable;
} lk_data_received_t;

/* All pointers are borrowed for the callback duration. */
typedef struct lk_data_track_info {
	uint16_t publisher_handle;
	const char* sid;
	const char* name;
	int uses_e2ee;
	int has_frame_encoding;
	lk_data_track_frame_encoding_t frame_encoding;
	const char* custom_frame_encoding;
	int has_schema;
	const char* schema_name;
	lk_data_track_schema_encoding_t schema_encoding;
	const char* custom_schema_encoding;
} lk_data_track_info_t;

typedef struct lk_data_track_frame_view {
	const uint8_t* data;
	size_t data_size;
	int has_user_timestamp;
	uint64_t user_timestamp;
} lk_data_track_frame_view_t;

typedef struct lk_data_track_snapshot_info {
	size_t struct_size;
	uint16_t publisher_handle;
	int uses_e2ee;
	int has_frame_encoding;
	lk_data_track_frame_encoding_t frame_encoding;
	int has_schema;
	lk_data_track_schema_encoding_t schema_encoding;
} lk_data_track_snapshot_info_t;

typedef struct lk_sip_dtmf {
	uint32_t code;
	const char* digit;
	const char* participant_identity;
} lk_sip_dtmf_t;

#define LK_CHAT_MESSAGE_ID_BUFFER_SIZE 37

typedef struct lk_chat_message {
	const char* id;
	int64_t timestamp;
	int has_edit_timestamp;
	int64_t edit_timestamp;
	const char* message;
	int deleted;
	int generated;
	const char* participant_identity;
} lk_chat_message_t;

typedef struct lk_transcription_segment {
	const char* id;
	const char* text;
	const char* language;
	uint64_t start_time;
	uint64_t end_time;
	int is_final;
	int64_t first_received_time;
	int64_t last_received_time;
} lk_transcription_segment_t;

typedef struct lk_transcription_received {
	const char* transcribed_participant_identity;
	const char* track_id;
	const lk_transcription_segment_t* segments;
	size_t segment_count;
} lk_transcription_received_t;

typedef struct lk_metric_timestamp {
	int64_t seconds;
	int32_t nanos;
} lk_metric_timestamp_t;

typedef struct lk_metric_sample {
	int64_t timestamp_ms;
	int has_normalized_timestamp;
	lk_metric_timestamp_t normalized_timestamp;
	float value;
} lk_metric_sample_t;

typedef struct lk_time_series_metric {
	uint32_t label;
	uint32_t participant_identity;
	uint32_t track_sid;
	uint32_t rid;
	const lk_metric_sample_t* samples;
	size_t sample_count;
} lk_time_series_metric_t;

typedef struct lk_event_metric {
	uint32_t label;
	uint32_t participant_identity;
	uint32_t track_sid;
	uint32_t rid;
	int64_t start_timestamp_ms;
	int has_end_timestamp_ms;
	int64_t end_timestamp_ms;
	int has_normalized_start_timestamp;
	lk_metric_timestamp_t normalized_start_timestamp;
	int has_normalized_end_timestamp;
	lk_metric_timestamp_t normalized_end_timestamp;
	const char* metadata;
} lk_event_metric_t;

typedef struct lk_metrics_received {
	int64_t timestamp_ms;
	int has_normalized_timestamp;
	lk_metric_timestamp_t normalized_timestamp;
	const char* const* string_data;
	size_t string_data_count;
	const lk_time_series_metric_t* time_series;
	size_t time_series_count;
	const lk_event_metric_t* events;
	size_t event_count;
	const char* participant_identity;
} lk_metrics_received_t;

typedef struct lk_file_received {
	const uint8_t* data;
	size_t data_size;
	const char* stream_id;
	const char* name;
	const char* mime_type;
	const char* topic;
	const char* participant_identity;
} lk_file_received_t;

typedef struct lk_text_received {
	const char* stream_id;
	const char* text;
	const char* topic;
	const char* participant_identity;
	const char* reply_to_stream_id;
	int64_t timestamp;
} lk_text_received_t;

typedef struct lk_text_stream_event {
	lk_data_stream_event_type_t type;
	const char* stream_id;
	const char* mime_type;
	const char* topic;
	const char* participant_identity;
	const char* content;
	size_t content_size;
	uint64_t chunk_index;
	int has_total_size;
	uint64_t total_size;
	const char* reason;
} lk_text_stream_event_t;

typedef struct lk_byte_stream_event {
	lk_data_stream_event_type_t type;
	const char* stream_id;
	const char* name;
	const char* mime_type;
	const char* topic;
	const char* participant_identity;
	const uint8_t* content;
	size_t content_size;
	uint64_t chunk_index;
	int has_total_size;
	uint64_t total_size;
	const char* reason;
} lk_byte_stream_event_t;

typedef struct lk_data_channel_buffer_status {
	int reliable;
	uint64_t buffered_amount;
	uint64_t high_water_mark;
	uint64_t low_water_mark;
	int backpressured;
} lk_data_channel_buffer_status_t;

typedef struct lk_data_stream_completion {
	lk_data_stream_completion_status_t status;
	const char* stream_id;
	uint64_t bytes_sent;
	int has_total_size;
	uint64_t total_size;
	const char* reason;
} lk_data_stream_completion_t;

typedef struct lk_attribute {
	const char* key;
	const char* value;
} lk_attribute_t;

typedef struct lk_rpc_invocation {
	const char* request_id;
	const char* caller_identity;
	const char* payload;
	uint32_t response_timeout_ms;
} lk_rpc_invocation_t;

typedef struct lk_rpc_handler_result {
	const char* payload;
	uint32_t error_code;
	const char* error_message;
	const char* error_data;
} lk_rpc_handler_result_t;

typedef lk_rpc_handler_result_t (*lk_rpc_handler)(void* user_data,
                                                  const lk_rpc_invocation_t* invocation);
/* The result is borrowed for the callback duration. Do not destroy the room from this callback. */
typedef void (*lk_rpc_completion_callback)(void* user_data, lk_room_t* room,
                                           const lk_rpc_result_t* result);
typedef void (*lk_data_stream_progress_callback)(void* user_data, uint64_t bytes_sent,
                                                 int has_total_size, uint64_t total_size);
/* Completion data is borrowed; destroy the writer only after this callback returns. */
typedef void (*lk_data_stream_completion_callback)(void* user_data,
                                                   const lk_data_stream_completion_t* completion);
typedef void (*lk_text_stream_handler)(void* user_data, lk_room_t* room,
                                       const lk_text_stream_event_t* event);
typedef void (*lk_byte_stream_handler)(void* user_data, lk_room_t* room,
                                       const lk_byte_stream_event_t* event);
typedef void (*lk_data_channel_buffer_status_callback)(
    void* user_data, lk_room_t* room, const lk_data_channel_buffer_status_t* status);

typedef void (*lk_room_event_callback)(void* user_data, lk_room_t* room);
typedef void (*lk_connection_state_callback)(void* user_data, lk_room_t* room,
                                             lk_room_state_t state);
typedef void (*lk_room_disconnected_callback)(void* user_data, lk_room_t* room,
                                              lk_disconnect_reason_t reason);
typedef void (*lk_participant_event_callback)(void* user_data, lk_room_t* room,
                                              const lk_participant_info_t* participant);
/*
 * Strings and attribute entries are borrowed for the callback duration. The participant contains
 * current values; previous_metadata is the replaced value, name is the current display name, and
 * an attribute change with an empty value removes that key.
 */
typedef void (*lk_participant_metadata_changed_callback)(void* user_data, lk_room_t* room,
                                                         const char* previous_metadata,
                                                         const lk_participant_info_t* participant);
typedef void (*lk_participant_name_changed_callback)(void* user_data, lk_room_t* room,
                                                     const char* name,
                                                     const lk_participant_info_t* participant);
typedef void (*lk_participant_attributes_changed_callback)(
    void* user_data, lk_room_t* room, const lk_attribute_t* changes, size_t change_count,
    const lk_participant_info_t* participant);
typedef void (*lk_participant_permissions_callback)(
    void* user_data, lk_room_t* room, const lk_participant_permissions_t* previous_permissions,
    const lk_participant_permissions_t* permissions, const lk_participant_info_t* participant);
typedef void (*lk_track_event_callback)(void* user_data, lk_room_t* room,
                                        const lk_track_publication_info_t* track,
                                        const lk_participant_info_t* participant);
typedef void (*lk_audio_frame_callback)(void* user_data, lk_room_t* room,
                                        const lk_track_publication_info_t* track,
                                        const lk_participant_info_t* participant,
                                        const lk_audio_frame_t* frame);
typedef void (*lk_video_frame_callback)(void* user_data, lk_room_t* room,
                                        const lk_track_publication_info_t* track,
                                        const lk_participant_info_t* participant,
                                        const lk_video_frame_t* frame);
typedef void (*lk_data_received_callback)(void* user_data, lk_room_t* room,
                                          const lk_data_received_t* event);
typedef void (*lk_sip_dtmf_callback)(void* user_data, lk_room_t* room, const lk_sip_dtmf_t* event);
typedef void (*lk_chat_message_callback)(void* user_data, lk_room_t* room,
                                         const lk_chat_message_t* event);
typedef void (*lk_transcription_received_callback)(void* user_data, lk_room_t* room,
                                                   const lk_transcription_received_t* event);
typedef void (*lk_metrics_received_callback)(void* user_data, lk_room_t* room,
                                             const lk_metrics_received_t* event);
typedef void (*lk_file_received_callback)(void* user_data, lk_room_t* room,
                                          const lk_file_received_t* event);
typedef void (*lk_text_received_callback)(void* user_data, lk_room_t* room,
                                          const lk_text_received_t* event);
typedef void (*lk_room_metadata_callback)(void* user_data, lk_room_t* room, const char* metadata);
typedef void (*lk_recording_status_callback)(void* user_data, lk_room_t* room, int recording);
typedef void (*lk_connection_quality_callback)(void* user_data, lk_room_t* room,
                                               lk_connection_quality_t quality,
                                               const lk_participant_info_t* participant);
typedef void (*lk_active_speakers_callback)(void* user_data, lk_room_t* room,
                                            const lk_participant_info_t* participants,
                                            size_t participant_count);
typedef void (*lk_track_subscription_permission_callback)(void* user_data, lk_room_t* room,
                                                          const lk_track_publication_info_t* track,
                                                          const lk_participant_info_t* participant,
                                                          int allowed);
typedef void (*lk_track_subscription_failed_callback)(void* user_data, lk_room_t* room,
                                                      const lk_track_publication_info_t* track,
                                                      const lk_participant_info_t* participant,
                                                      lk_subscription_error_t error);
typedef void (*lk_track_stream_state_callback)(void* user_data, lk_room_t* room,
                                               const lk_track_publication_info_t* track,
                                               const lk_participant_info_t* participant,
                                               lk_track_stream_state_t state);
typedef void (*lk_track_subscription_status_callback)(void* user_data, lk_room_t* room,
                                                      const lk_track_publication_info_t* track,
                                                      const lk_participant_info_t* participant,
                                                      lk_track_subscription_status_t status);
typedef void (*lk_subscribed_quality_update_callback)(void* user_data, lk_room_t* room,
                                                      const lk_track_publication_info_t* track,
                                                      const lk_participant_info_t* participant,
                                                      const lk_subscribed_quality_update_t* update);
typedef void (*lk_encryption_state_callback)(void* user_data, lk_room_t* room,
                                             const lk_encryption_state_t* state);
typedef void (*lk_data_track_event_callback)(void* user_data, lk_room_t* room,
                                             const lk_data_track_info_t* track,
                                             const lk_participant_info_t* participant);
typedef void (*lk_data_track_frame_callback)(void* user_data, lk_room_t* room,
                                             const lk_data_track_info_t* track,
                                             const lk_participant_info_t* participant,
                                             const lk_data_track_frame_view_t* frame);

typedef struct lk_room_callbacks {
	size_t struct_size;
	void* user_data;
	lk_room_event_callback on_connected;
	lk_room_event_callback on_disconnected;
	lk_participant_event_callback on_participant_connected;
	lk_participant_event_callback on_participant_disconnected;
	lk_track_event_callback on_track_published;
	lk_track_event_callback on_track_unpublished;
	lk_track_event_callback on_track_muted;
	lk_track_event_callback on_track_unmuted;
	lk_track_event_callback on_track_subscribed;
	lk_audio_frame_callback on_audio_frame;
	lk_video_frame_callback on_video_frame;
	lk_data_received_callback on_data_received;
	lk_file_received_callback on_file_received;
	lk_room_metadata_callback on_room_metadata_changed;
	lk_connection_quality_callback on_connection_quality_changed;
	lk_active_speakers_callback on_active_speakers_changed;
	lk_track_event_callback on_local_track_published;
	lk_track_event_callback on_local_track_unpublished;
	lk_text_received_callback on_text_received;
	lk_file_received_callback on_byte_received;
	lk_room_event_callback on_reconnecting;
	lk_room_event_callback on_reconnected;
	lk_room_disconnected_callback on_disconnected_with_reason;
	lk_track_subscription_permission_callback on_track_subscription_permission_changed;
	lk_track_subscription_failed_callback on_track_subscription_failed;
	lk_track_event_callback on_track_unsubscribed;
	lk_track_stream_state_callback on_track_stream_state_changed;
	lk_track_subscription_status_callback on_track_subscription_status_changed;
	lk_data_channel_buffer_status_callback on_data_channel_buffer_status_changed;
	lk_sip_dtmf_callback on_sip_dtmf_received;
	lk_chat_message_callback on_chat_message_received;
	lk_transcription_received_callback on_transcription_received;
	lk_recording_status_callback on_recording_status_changed;
	lk_metrics_received_callback on_metrics_received;
	lk_connection_state_callback on_connection_state_changed;
	lk_participant_permissions_callback on_participant_permissions_changed;
	lk_track_event_callback on_local_track_subscribed;
	lk_subscribed_quality_update_callback on_subscribed_quality_update;
	lk_encryption_state_callback on_encryption_state_changed;
	lk_participant_metadata_changed_callback on_participant_metadata_changed;
	lk_participant_name_changed_callback on_participant_name_changed;
	lk_participant_attributes_changed_callback on_participant_attributes_changed;
	lk_data_track_event_callback on_data_track_published;
	lk_data_track_event_callback on_data_track_unpublished;
	lk_data_track_event_callback on_local_data_track_published;
	lk_data_track_event_callback on_local_data_track_unpublished;
	lk_data_track_frame_callback on_data_track_frame;
} lk_room_callbacks_t;

typedef struct lk_e2ee_options {
	size_t struct_size;
	int enabled;
	const uint8_t* shared_key;
	size_t shared_key_size;
	const uint8_t* ratchet_salt;
	size_t ratchet_salt_size;
	const uint8_t* unencrypted_magic_bytes;
	size_t unencrypted_magic_bytes_size;
	size_t ratchet_window_size;
	int failure_tolerance;
	size_t key_ring_size;
	lk_e2ee_key_derivation_t key_derivation;
} lk_e2ee_options_t;

typedef struct lk_audio_source_options {
	size_t struct_size;
	uint32_t sample_rate;
	uint32_t num_channels;
	uint32_t queue_size_ms;
	int echo_cancellation;
	int auto_gain_control;
	int noise_suppression;
} lk_audio_source_options_t;

typedef struct lk_microphone_capture_options {
	size_t struct_size;
	const char* device_id;
	uint32_t queue_size_ms;
	int echo_cancellation;
	int auto_gain_control;
	int noise_suppression;
} lk_microphone_capture_options_t;

typedef struct lk_system_audio_capture_options {
	size_t struct_size;
	const char* device_id;
	uint32_t queue_size_ms;
} lk_system_audio_capture_options_t;

typedef struct lk_microphone_processing_stats {
	size_t struct_size;
	uint64_t capture_frames_processed;
	uint64_t render_frames_processed;
	uint64_t capture_processing_errors;
	uint64_t render_processing_errors;
	uint64_t frames_dropped;
	int echo_cancellation_enabled;
} lk_microphone_processing_stats_t;

typedef struct lk_video_source_options {
	size_t struct_size;
	int is_screencast;
} lk_video_source_options_t;

typedef struct lk_camera_capture_options {
	size_t struct_size;
	const char* device_id;
	uint32_t width;
	uint32_t height;
	uint32_t frames_per_second;
} lk_camera_capture_options_t;

typedef struct lk_screen_capture_options {
	size_t struct_size;
	const char* source_id;
	uint32_t frames_per_second;
	int include_cursor;
} lk_screen_capture_options_t;

typedef struct lk_video_encoding {
	size_t struct_size;
	uint64_t max_bitrate;
	float max_framerate;
} lk_video_encoding_t;

typedef struct lk_track_publish_options {
	size_t struct_size;
	lk_track_source_t source;
	int dtx;
	int red;
	int simulcast;
	const char* stream;
	lk_video_codec_t video_codec;
	const char* scalability_mode;
	int backup_video_codec_enabled;
	lk_video_codec_t backup_video_codec;
	lk_backup_codec_policy_t backup_codec_policy;
	lk_video_encoding_t video_encoding;
	lk_video_encoding_t backup_video_encoding;
} lk_track_publish_options_t;

typedef struct lk_data_publish_options {
	size_t struct_size;
	int reliable;
	const char* topic;
	const char* const* destination_identities;
	size_t destination_identity_count;
} lk_data_publish_options_t;

typedef struct lk_data_track_schema_id {
	size_t struct_size;
	const char* name;
	lk_data_track_schema_encoding_t encoding;
	const char* custom_encoding;
} lk_data_track_schema_id_t;

typedef struct lk_data_track_publish_options {
	size_t struct_size;
	const char* name;
	int has_frame_encoding;
	lk_data_track_frame_encoding_t frame_encoding;
	const char* custom_frame_encoding;
	int has_schema;
	lk_data_track_schema_id_t schema;
} lk_data_track_publish_options_t;

typedef struct lk_data_track_subscription_options {
	size_t struct_size;
	int has_target_fps;
	uint32_t target_fps;
	size_t buffer_capacity;
	size_t max_partial_frames;
} lk_data_track_subscription_options_t;

typedef struct lk_file_send_options {
	size_t struct_size;
	const char* topic;
	const char* mime_type;
	const char* const* destination_identities;
	size_t destination_identity_count;
	size_t chunk_size;
	const lk_attribute_t* attributes;
	size_t attribute_count;
	int compress;
} lk_file_send_options_t;

typedef struct lk_text_send_options {
	size_t struct_size;
	const char* topic;
	const char* const* destination_identities;
	size_t destination_identity_count;
	const lk_attribute_t* attributes;
	size_t attribute_count;
	const char* reply_to_stream_id;
	const char* const* attached_stream_ids;
	size_t attached_stream_id_count;
	size_t chunk_size;
	int compress;
} lk_text_send_options_t;

typedef struct lk_byte_send_options {
	size_t struct_size;
	const char* topic;
	const char* mime_type;
	const char* name;
	const char* const* destination_identities;
	size_t destination_identity_count;
	const lk_attribute_t* attributes;
	size_t attribute_count;
	size_t chunk_size;
	int compress;
} lk_byte_send_options_t;

typedef struct lk_stream_text_options {
	size_t struct_size;
	const char* topic;
	const char* const* destination_identities;
	size_t destination_identity_count;
	const lk_attribute_t* attributes;
	size_t attribute_count;
	const char* reply_to_stream_id;
	const char* const* attached_stream_ids;
	size_t attached_stream_id_count;
	const char* stream_id;
	int has_total_size;
	uint64_t total_size;
	size_t chunk_size;
	int update;
	int32_t version;
	lk_data_stream_progress_callback on_progress;
	void* progress_user_data;
	int compress;
	lk_data_stream_completion_callback on_complete;
	void* completion_user_data;
} lk_stream_text_options_t;

typedef struct lk_stream_bytes_options {
	size_t struct_size;
	const char* topic;
	const char* mime_type;
	const char* name;
	const char* const* destination_identities;
	size_t destination_identity_count;
	const lk_attribute_t* attributes;
	size_t attribute_count;
	const char* stream_id;
	int has_total_size;
	uint64_t total_size;
	size_t chunk_size;
	lk_data_stream_progress_callback on_progress;
	void* progress_user_data;
	int compress;
	lk_data_stream_completion_callback on_complete;
	void* completion_user_data;
} lk_stream_bytes_options_t;

typedef struct lk_rpc_perform_options {
	size_t struct_size;
	const char* destination_identity;
	const char* method;
	const char* payload;
	uint32_t response_timeout_ms;
} lk_rpc_perform_options_t;

typedef struct lk_participant_track_permission {
	size_t struct_size;
	const char* participant_sid;
	const char* participant_identity;
	int allow_all;
	const char* const* allowed_track_sids;
	size_t allowed_track_sid_count;
} lk_participant_track_permission_t;

typedef struct lk_remote_track_settings {
	size_t struct_size;
	int enabled;
	int has_video_quality;
	lk_video_quality_t video_quality;
	uint32_t video_width;
	uint32_t video_height;
	uint32_t video_fps;
	uint32_t priority;
} lk_remote_track_settings_t;

LKC_API lk_status_t lk_init(void);
LKC_API lk_status_t lk_shutdown(void);
LKC_API size_t lk_version(char* buffer, size_t buffer_size);
LKC_API const char* lk_last_error(void);

LKC_API void lk_log_options_init(lk_log_options_t* options);
LKC_API lk_status_t lk_log_set_options(const lk_log_options_t* options);
LKC_API lk_status_t lk_log_get_options(lk_log_options_t* options);
/*
 * Replaces the process-wide log sink. Passing NULL unregisters it and waits for callbacks already
 * in progress before returning, after which the previous user_data may be released.
 */
LKC_API lk_status_t lk_log_set_callback(lk_log_callback callback, void* user_data);

/*
 * Creates a read-only snapshot of active host media devices. The list owns its strings and must
 * be destroyed by the caller. String getters use the standard two-stage convention.
 */
LKC_API lk_status_t lk_media_device_list_create(lk_media_device_list_t** devices);
LKC_API void lk_media_device_list_destroy(lk_media_device_list_t* devices);
LKC_API size_t lk_media_device_list_count(const lk_media_device_list_t* devices);
LKC_API lk_status_t lk_media_device_list_info(const lk_media_device_list_t* devices, size_t index,
                                              lk_media_device_info_t* info);
LKC_API size_t lk_media_device_list_id(const lk_media_device_list_t* devices, size_t index,
                                       char* buffer, size_t buffer_size);
LKC_API size_t lk_media_device_list_label(const lk_media_device_list_t* devices, size_t index,
                                          char* buffer, size_t buffer_size);

/* Creates a read-only snapshot of shareable monitors and windows. */
LKC_API lk_status_t lk_screen_source_list_create(lk_screen_source_list_t** sources);
LKC_API void lk_screen_source_list_destroy(lk_screen_source_list_t* sources);
LKC_API size_t lk_screen_source_list_count(const lk_screen_source_list_t* sources);
LKC_API lk_status_t lk_screen_source_list_info(const lk_screen_source_list_t* sources, size_t index,
                                               lk_screen_source_info_t* info);
LKC_API size_t lk_screen_source_list_id(const lk_screen_source_list_t* sources, size_t index,
                                        char* buffer, size_t buffer_size);
LKC_API size_t lk_screen_source_list_label(const lk_screen_source_list_t* sources, size_t index,
                                           char* buffer, size_t buffer_size);

LKC_API void lk_room_callbacks_init(lk_room_callbacks_t* callbacks);
LKC_API void lk_e2ee_options_init(lk_e2ee_options_t* options);
LKC_API void lk_frame_cryptor_info_init(lk_frame_cryptor_info_t* info);
LKC_API void lk_audio_playback_stats_init(lk_audio_playback_stats_t* stats);
LKC_API void lk_audio_source_options_init(lk_audio_source_options_t* options);
LKC_API void lk_microphone_capture_options_init(lk_microphone_capture_options_t* options);
LKC_API void lk_system_audio_capture_options_init(lk_system_audio_capture_options_t* options);
LKC_API void lk_microphone_processing_stats_init(lk_microphone_processing_stats_t* stats);
LKC_API void lk_video_source_options_init(lk_video_source_options_t* options);
LKC_API void lk_camera_capture_options_init(lk_camera_capture_options_t* options);
LKC_API void lk_screen_capture_options_init(lk_screen_capture_options_t* options);
LKC_API void lk_video_encoding_init(lk_video_encoding_t* encoding);
LKC_API void lk_track_publish_options_init(lk_track_publish_options_t* options);
LKC_API void lk_data_publish_options_init(lk_data_publish_options_t* options);
LKC_API void lk_data_track_schema_id_init(lk_data_track_schema_id_t* schema_id);
LKC_API void lk_data_track_publish_options_init(lk_data_track_publish_options_t* options);
LKC_API void lk_data_track_subscription_options_init(lk_data_track_subscription_options_t* options);
LKC_API void lk_data_track_snapshot_info_init(lk_data_track_snapshot_info_t* info);
LKC_API void lk_file_send_options_init(lk_file_send_options_t* options);
LKC_API void lk_text_send_options_init(lk_text_send_options_t* options);
LKC_API void lk_byte_send_options_init(lk_byte_send_options_t* options);
LKC_API void lk_stream_text_options_init(lk_stream_text_options_t* options);
LKC_API void lk_stream_bytes_options_init(lk_stream_bytes_options_t* options);
LKC_API void lk_rpc_perform_options_init(lk_rpc_perform_options_t* options);
LKC_API void lk_participant_track_permission_init(lk_participant_track_permission_t* permission);
LKC_API void lk_remote_track_settings_init(lk_remote_track_settings_t* settings);
LKC_API void lk_remote_participant_snapshot_info_init(lk_remote_participant_snapshot_info_t* info);
LKC_API void
lk_remote_track_publication_snapshot_info_init(lk_remote_track_publication_snapshot_info_t* info);
LKC_API void lk_remote_track_snapshot_info_init(lk_remote_track_snapshot_info_t* info);

LKC_API lk_status_t lk_room_create(lk_room_t** room);
LKC_API void lk_room_destroy(lk_room_t* room);
LKC_API lk_status_t lk_room_set_callbacks(lk_room_t* room, const lk_room_callbacks_t* callbacks);
LKC_API lk_status_t lk_room_connect(lk_room_t* room, const char* url, const char* token);
LKC_API lk_status_t lk_room_connect_e2ee(lk_room_t* room, const char* url, const char* token,
                                         const lk_e2ee_options_t* options);
LKC_API lk_status_t lk_room_disconnect(lk_room_t* room);
LKC_API lk_room_state_t lk_room_state(const lk_room_t* room);
LKC_API lk_disconnect_reason_t lk_room_disconnect_reason(const lk_room_t* room);
LKC_API int lk_room_is_connected(const lk_room_t* room);
LKC_API size_t lk_room_sid(const lk_room_t* room, char* buffer, size_t buffer_size);
LKC_API size_t lk_room_name(const lk_room_t* room, char* buffer, size_t buffer_size);
LKC_API size_t lk_room_metadata(const lk_room_t* room, char* buffer, size_t buffer_size);
LKC_API int lk_room_is_recording(const lk_room_t* room);
LKC_API lk_status_t lk_room_set_audio_output_device(lk_room_t* room, const char* device_id);
LKC_API size_t lk_room_audio_output_device(const lk_room_t* room, char* buffer, size_t buffer_size);
LKC_API lk_status_t lk_room_set_speaker_volume(lk_room_t* room, float volume);
LKC_API float lk_room_speaker_volume(const lk_room_t* room);
LKC_API lk_status_t lk_room_set_speaker_muted(lk_room_t* room, int muted);
LKC_API int lk_room_speaker_is_muted(const lk_room_t* room);
LKC_API lk_status_t lk_room_audio_playback_stats(const lk_room_t* room,
                                                 lk_audio_playback_stats_t* stats);

/* E2EE keys are copied during calls. Export functions return the required byte count. */
LKC_API int lk_room_e2ee_is_configured(const lk_room_t* room);
LKC_API int lk_room_e2ee_is_enabled(const lk_room_t* room);
LKC_API lk_status_t lk_room_e2ee_set_enabled(lk_room_t* room, int enabled);
LKC_API lk_status_t lk_room_e2ee_set_shared_key(lk_room_t* room, const uint8_t* key,
                                                size_t key_size, size_t key_index);
LKC_API size_t lk_room_e2ee_export_shared_key(const lk_room_t* room, size_t key_index,
                                              uint8_t* buffer, size_t buffer_size);
LKC_API lk_status_t lk_room_e2ee_ratchet_shared_key(lk_room_t* room, size_t key_index);
LKC_API lk_status_t lk_room_e2ee_remove_shared_key(lk_room_t* room, size_t key_index);
LKC_API lk_status_t lk_room_e2ee_set_participant_key(lk_room_t* room,
                                                     const char* participant_identity,
                                                     const uint8_t* key, size_t key_size,
                                                     size_t key_index);
LKC_API size_t lk_room_e2ee_export_participant_key(const lk_room_t* room,
                                                   const char* participant_identity,
                                                   size_t key_index, uint8_t* buffer,
                                                   size_t buffer_size);
LKC_API lk_status_t lk_room_e2ee_ratchet_participant_key(lk_room_t* room,
                                                         const char* participant_identity,
                                                         size_t key_index);
LKC_API lk_status_t lk_room_e2ee_remove_participant_key(lk_room_t* room,
                                                        const char* participant_identity,
                                                        size_t key_index);
LKC_API lk_status_t lk_room_e2ee_remove_participant_keys(lk_room_t* room,
                                                         const char* participant_identity);
LKC_API lk_status_t lk_room_e2ee_clear_keys(lk_room_t* room);
LKC_API size_t lk_room_e2ee_data_key_index(const lk_room_t* room);
LKC_API lk_status_t lk_room_e2ee_set_data_key_index(lk_room_t* room, size_t key_index);
LKC_API lk_status_t lk_room_e2ee_set_frame_cryptor_enabled(lk_room_t* room, const char* track_id,
                                                           lk_frame_cryptor_direction_t direction,
                                                           int enabled);
LKC_API lk_status_t lk_room_e2ee_set_frame_cryptor_key_index(lk_room_t* room, const char* track_id,
                                                             lk_frame_cryptor_direction_t direction,
                                                             size_t key_index);
LKC_API lk_status_t lk_room_e2ee_set_participant_enabled(lk_room_t* room,
                                                         const char* participant_identity,
                                                         int enabled, size_t* updated_count);
LKC_API lk_status_t lk_frame_cryptor_list_create(const lk_room_t* room,
                                                 lk_frame_cryptor_list_t** cryptors);
LKC_API void lk_frame_cryptor_list_destroy(lk_frame_cryptor_list_t* cryptors);
LKC_API size_t lk_frame_cryptor_list_count(const lk_frame_cryptor_list_t* cryptors);
LKC_API lk_status_t lk_frame_cryptor_list_info(const lk_frame_cryptor_list_t* cryptors,
                                               size_t index, lk_frame_cryptor_info_t* info);
LKC_API size_t lk_frame_cryptor_list_track_id(const lk_frame_cryptor_list_t* cryptors, size_t index,
                                              char* buffer, size_t buffer_size);
LKC_API size_t lk_frame_cryptor_list_participant_identity(const lk_frame_cryptor_list_t* cryptors,
                                                          size_t index, char* buffer,
                                                          size_t buffer_size);

/*
 * The list owns every participant, publication, and subscribed-track handle returned from it.
 * Child handles and permission source arrays remain valid until the list is destroyed. They are
 * immutable and do not reflect later room updates. Strings use the standard two-stage getters.
 */
LKC_API lk_status_t lk_room_create_remote_participant_snapshot(
    const lk_room_t* room, lk_remote_participant_list_t** snapshot);
LKC_API void lk_remote_participant_list_destroy(lk_remote_participant_list_t* snapshot);
LKC_API size_t lk_remote_participant_list_count(const lk_remote_participant_list_t* snapshot);
LKC_API lk_status_t
lk_remote_participant_list_at(const lk_remote_participant_list_t* snapshot, size_t index,
                              const lk_remote_participant_snapshot_t** participant);
LKC_API lk_status_t
lk_remote_participant_snapshot_info(const lk_remote_participant_snapshot_t* participant,
                                    lk_remote_participant_snapshot_info_t* info);
LKC_API lk_status_t lk_remote_participant_snapshot_permissions(
    const lk_remote_participant_snapshot_t* participant, lk_participant_permissions_t* permissions);
LKC_API size_t lk_remote_participant_snapshot_sid(
    const lk_remote_participant_snapshot_t* participant, char* buffer, size_t buffer_size);
LKC_API size_t lk_remote_participant_snapshot_identity(
    const lk_remote_participant_snapshot_t* participant, char* buffer, size_t buffer_size);
LKC_API size_t lk_remote_participant_snapshot_name(
    const lk_remote_participant_snapshot_t* participant, char* buffer, size_t buffer_size);
LKC_API size_t lk_remote_participant_snapshot_metadata(
    const lk_remote_participant_snapshot_t* participant, char* buffer, size_t buffer_size);
LKC_API size_t
lk_remote_participant_snapshot_attribute_count(const lk_remote_participant_snapshot_t* participant);
LKC_API size_t
lk_remote_participant_snapshot_attribute_key(const lk_remote_participant_snapshot_t* participant,
                                             size_t index, char* buffer, size_t buffer_size);
LKC_API size_t
lk_remote_participant_snapshot_attribute_value(const lk_remote_participant_snapshot_t* participant,
                                               size_t index, char* buffer, size_t buffer_size);
LKC_API size_t lk_remote_participant_snapshot_publication_count(
    const lk_remote_participant_snapshot_t* participant);
LKC_API lk_status_t lk_remote_participant_snapshot_publication_at(
    const lk_remote_participant_snapshot_t* participant, size_t index,
    const lk_remote_track_publication_snapshot_t** publication);
LKC_API lk_status_t
lk_remote_track_publication_snapshot_info(const lk_remote_track_publication_snapshot_t* publication,
                                          lk_remote_track_publication_snapshot_info_t* info);
LKC_API size_t lk_remote_track_publication_snapshot_sid(
    const lk_remote_track_publication_snapshot_t* publication, char* buffer, size_t buffer_size);
LKC_API size_t lk_remote_track_publication_snapshot_name(
    const lk_remote_track_publication_snapshot_t* publication, char* buffer, size_t buffer_size);
LKC_API size_t lk_remote_track_publication_snapshot_mime_type(
    const lk_remote_track_publication_snapshot_t* publication, char* buffer, size_t buffer_size);
LKC_API lk_status_t lk_remote_track_publication_snapshot_track(
    const lk_remote_track_publication_snapshot_t* publication,
    const lk_remote_track_snapshot_t** track);
LKC_API lk_status_t lk_remote_track_snapshot_info(const lk_remote_track_snapshot_t* track,
                                                  lk_remote_track_snapshot_info_t* info);
LKC_API size_t lk_remote_track_snapshot_sid(const lk_remote_track_snapshot_t* track, char* buffer,
                                            size_t buffer_size);
LKC_API size_t lk_remote_track_snapshot_name(const lk_remote_track_snapshot_t* track, char* buffer,
                                             size_t buffer_size);

LKC_API size_t lk_local_participant_sid(const lk_room_t* room, char* buffer, size_t buffer_size);
LKC_API size_t lk_local_participant_identity(const lk_room_t* room, char* buffer,
                                             size_t buffer_size);
LKC_API size_t lk_local_participant_name(const lk_room_t* room, char* buffer, size_t buffer_size);
LKC_API size_t lk_local_participant_metadata(const lk_room_t* room, char* buffer,
                                             size_t buffer_size);
LKC_API lk_status_t lk_local_participant_set_metadata(lk_room_t* room, const char* metadata);
LKC_API lk_status_t lk_local_participant_set_name(lk_room_t* room, const char* name);
LKC_API lk_status_t lk_local_participant_set_attributes(lk_room_t* room,
                                                        const lk_attribute_t* attributes,
                                                        size_t attribute_count);

LKC_API lk_status_t lk_audio_source_create(const lk_audio_source_options_t* options,
                                           lk_audio_source_t** source);
LKC_API lk_status_t lk_audio_source_create_microphone(
    const lk_microphone_capture_options_t* options, lk_audio_source_t** source);
LKC_API lk_status_t lk_audio_source_create_system_audio(
    const lk_system_audio_capture_options_t* options, lk_audio_source_t** source);
LKC_API lk_status_t lk_audio_source_destroy(lk_audio_source_t* source);
LKC_API lk_status_t lk_audio_source_capture_frame(lk_audio_source_t* source, const int16_t* data,
                                                  uint32_t samples_per_channel);
LKC_API lk_status_t lk_audio_source_microphone_start(lk_audio_source_t* source);
LKC_API lk_status_t lk_audio_source_microphone_stop(lk_audio_source_t* source);
LKC_API int lk_audio_source_microphone_is_capturing(const lk_audio_source_t* source);
LKC_API size_t lk_audio_source_microphone_device_id(const lk_audio_source_t* source, char* buffer,
                                                    size_t buffer_size);
LKC_API lk_status_t lk_audio_source_microphone_switch_device(lk_audio_source_t* source,
                                                             const char* device_id);
LKC_API lk_status_t lk_audio_source_microphone_set_muted(lk_audio_source_t* source, int muted);
LKC_API int lk_audio_source_microphone_is_muted(const lk_audio_source_t* source);
LKC_API lk_status_t lk_audio_source_microphone_set_volume(lk_audio_source_t* source, float volume);
LKC_API float lk_audio_source_microphone_volume(const lk_audio_source_t* source);
LKC_API lk_status_t lk_audio_source_microphone_set_processing_options(
    lk_audio_source_t* source, const lk_audio_source_options_t* options);
LKC_API lk_status_t lk_audio_source_microphone_processing_options(
    const lk_audio_source_t* source, lk_audio_source_options_t* options);
LKC_API lk_status_t lk_audio_source_microphone_processing_stats(
    const lk_audio_source_t* source, lk_microphone_processing_stats_t* stats);
LKC_API lk_status_t lk_audio_source_system_audio_start(lk_audio_source_t* source);
LKC_API lk_status_t lk_audio_source_system_audio_stop(lk_audio_source_t* source);
LKC_API int lk_audio_source_system_audio_is_capturing(const lk_audio_source_t* source);
LKC_API size_t lk_audio_source_system_audio_device_id(const lk_audio_source_t* source, char* buffer,
                                                      size_t buffer_size);
LKC_API lk_status_t lk_audio_source_system_audio_switch_device(lk_audio_source_t* source,
                                                               const char* device_id);
LKC_API lk_status_t lk_video_source_create(const lk_video_source_options_t* options,
                                           lk_video_source_t** source);
LKC_API lk_status_t lk_video_source_create_camera(const lk_camera_capture_options_t* options,
                                                  lk_video_source_t** source);
LKC_API lk_status_t lk_video_source_create_screen(const lk_screen_capture_options_t* options,
                                                  lk_video_source_t** source);
LKC_API lk_status_t lk_video_source_destroy(lk_video_source_t* source);
LKC_API lk_status_t lk_video_source_capture_i420(lk_video_source_t* source, const uint8_t* data,
                                                 size_t data_size, uint32_t width, uint32_t height,
                                                 int64_t timestamp_us);
LKC_API lk_status_t lk_video_source_camera_start(lk_video_source_t* source);
LKC_API lk_status_t lk_video_source_camera_stop(lk_video_source_t* source);
LKC_API int lk_video_source_camera_is_capturing(const lk_video_source_t* source);
LKC_API size_t lk_video_source_camera_device_id(const lk_video_source_t* source, char* buffer,
                                                size_t buffer_size);
LKC_API lk_status_t lk_video_source_camera_switch_device(lk_video_source_t* source,
                                                         const char* device_id);
LKC_API lk_status_t lk_video_source_screen_start(lk_video_source_t* source);
LKC_API lk_status_t lk_video_source_screen_stop(lk_video_source_t* source);
LKC_API int lk_video_source_screen_is_capturing(const lk_video_source_t* source);
LKC_API size_t lk_video_source_screen_source_id(const lk_video_source_t* source, char* buffer,
                                                size_t buffer_size);
LKC_API lk_status_t lk_video_source_screen_switch_source(lk_video_source_t* source,
                                                         const char* source_id);

LKC_API lk_status_t lk_room_create_audio_track(lk_room_t* room, const char* label,
                                               lk_audio_source_t* source, lk_local_track_t** track);
LKC_API lk_status_t lk_room_create_video_track(lk_room_t* room, const char* label,
                                               lk_video_source_t* source, lk_local_track_t** track);
LKC_API lk_status_t lk_local_track_publish(lk_room_t* room, lk_local_track_t* track,
                                           const lk_track_publish_options_t* options);
/* These helpers classify application-provided frames as screen-share media. */
LKC_API lk_status_t lk_local_track_publish_screen_share_video(
    lk_room_t* room, lk_local_track_t* track, const lk_track_publish_options_t* options);
LKC_API lk_status_t lk_local_track_publish_screen_share_audio(
    lk_room_t* room, lk_local_track_t* track, const lk_track_publish_options_t* options);
LKC_API lk_status_t lk_local_track_unpublish(lk_local_track_t* track, int stop_on_unpublish);
LKC_API lk_status_t lk_room_republish_all_tracks(lk_room_t* room);
LKC_API lk_status_t lk_local_track_set_muted(lk_local_track_t* track, int muted);
LKC_API lk_status_t lk_local_video_track_update_encoding(lk_local_track_t* track,
                                                         const lk_video_encoding_t* encoding,
                                                         int backup_codec);
LKC_API size_t lk_local_track_rtc_stats(const lk_local_track_t* track, char* buffer,
                                        size_t buffer_size);
LKC_API lk_status_t lk_local_track_destroy(lk_local_track_t* track);

LKC_API lk_status_t lk_room_set_remote_track_subscribed(lk_room_t* room,
                                                        const char* participant_sid,
                                                        const char* track_sid, int subscribed);
LKC_API lk_status_t lk_room_update_remote_track_settings(
    lk_room_t* room, const char* participant_sid, const char* track_sid,
    const lk_remote_track_settings_t* settings);
LKC_API lk_status_t lk_room_set_track_subscription_permissions(
    lk_room_t* room, int all_participants_allowed,
    const lk_participant_track_permission_t* permissions, size_t permission_count);

LKC_API lk_status_t lk_room_publish_data(lk_room_t* room, const uint8_t* data, size_t data_size,
                                         const lk_data_publish_options_t* options);
/* DataTrack failures return a stable domain code and also update lk_last_error(). */
LKC_API lk_data_track_error_code_t
lk_room_store_data_track_schema(lk_room_t* room, const lk_data_track_schema_id_t* schema_id,
                                const uint8_t* definition, size_t definition_size);
LKC_API lk_data_track_error_code_t lk_room_get_data_track_schema(
    lk_room_t* room, const char* participant_identity, const lk_data_track_schema_id_t* schema_id,
    lk_data_track_schema_t** schema);
LKC_API void lk_data_track_schema_destroy(lk_data_track_schema_t* schema);
LKC_API size_t lk_data_track_schema_name(const lk_data_track_schema_t* schema, char* buffer,
                                         size_t buffer_size);
LKC_API lk_data_track_schema_encoding_t
lk_data_track_schema_encoding(const lk_data_track_schema_t* schema);
LKC_API size_t lk_data_track_schema_custom_encoding(const lk_data_track_schema_t* schema,
                                                    char* buffer, size_t buffer_size);
LKC_API size_t lk_data_track_schema_definition(const lk_data_track_schema_t* schema,
                                               uint8_t* buffer, size_t buffer_size);
LKC_API lk_data_track_error_code_t lk_room_publish_data_track(
    lk_room_t* room, const lk_data_track_publish_options_t* options, lk_local_data_track_t** track);
LKC_API lk_data_track_error_code_t lk_local_data_track_try_push(lk_local_data_track_t* track,
                                                                const uint8_t* data,
                                                                size_t data_size,
                                                                int has_user_timestamp,
                                                                uint64_t user_timestamp);
LKC_API lk_data_track_error_code_t lk_local_data_track_unpublish(lk_local_data_track_t* track);
LKC_API lk_data_track_error_code_t lk_local_data_track_destroy(lk_local_data_track_t* track);
LKC_API int lk_local_data_track_is_published(const lk_local_data_track_t* track);
LKC_API lk_status_t lk_local_data_track_info(const lk_local_data_track_t* track,
                                             lk_data_track_snapshot_info_t* info);
LKC_API size_t lk_local_data_track_sid(const lk_local_data_track_t* track, char* buffer,
                                       size_t buffer_size);
LKC_API size_t lk_local_data_track_name(const lk_local_data_track_t* track, char* buffer,
                                        size_t buffer_size);
LKC_API size_t lk_local_data_track_custom_frame_encoding(const lk_local_data_track_t* track,
                                                         char* buffer, size_t buffer_size);
LKC_API size_t lk_local_data_track_schema_name(const lk_local_data_track_t* track, char* buffer,
                                               size_t buffer_size);
LKC_API size_t lk_local_data_track_custom_schema_encoding(const lk_local_data_track_t* track,
                                                          char* buffer, size_t buffer_size);
LKC_API lk_data_track_error_code_t lk_room_subscribe_data_track(
    lk_room_t* room, const char* participant_identity, const char* track_sid,
    const lk_data_track_subscription_options_t* options, lk_data_track_reader_t** reader);
LKC_API void lk_data_track_reader_destroy(lk_data_track_reader_t* reader);
LKC_API void lk_data_track_reader_close(lk_data_track_reader_t* reader);
LKC_API int lk_data_track_reader_is_closed(const lk_data_track_reader_t* reader);
LKC_API size_t lk_data_track_reader_dropped_frames(const lk_data_track_reader_t* reader);
LKC_API lk_data_track_read_status_t lk_data_track_reader_try_read(lk_data_track_reader_t* reader,
                                                                  lk_data_track_frame_t** frame);
LKC_API lk_data_track_read_status_t lk_data_track_reader_read_for(lk_data_track_reader_t* reader,
                                                                  uint32_t timeout_ms,
                                                                  lk_data_track_frame_t** frame);
LKC_API void lk_data_track_frame_destroy(lk_data_track_frame_t* frame);
LKC_API size_t lk_data_track_frame_data(const lk_data_track_frame_t* frame, uint8_t* buffer,
                                        size_t buffer_size);
LKC_API int lk_data_track_frame_has_user_timestamp(const lk_data_track_frame_t* frame);
LKC_API uint64_t lk_data_track_frame_user_timestamp(const lk_data_track_frame_t* frame);
LKC_API lk_status_t lk_room_publish_dtmf(lk_room_t* room, uint32_t code, const char* digit);
LKC_API lk_status_t lk_room_send_chat_message(lk_room_t* room, const char* message,
                                              char* message_id, size_t message_id_size,
                                              int64_t* timestamp);
LKC_API lk_status_t lk_room_edit_chat_message(lk_room_t* room, const char* message_id,
                                              int64_t original_timestamp, const char* message);
LKC_API lk_status_t lk_room_send_text(lk_room_t* room, const char* text,
                                      const lk_text_send_options_t* options);
LKC_API lk_status_t lk_room_send_bytes(lk_room_t* room, const uint8_t* data, size_t data_size,
                                       const lk_byte_send_options_t* options);
LKC_API lk_status_t lk_room_send_file(lk_room_t* room, const char* path,
                                      const lk_file_send_options_t* options);
LKC_API lk_status_t lk_room_stream_text(lk_room_t* room, const lk_stream_text_options_t* options,
                                        lk_text_stream_writer_t** writer);
LKC_API lk_status_t lk_text_stream_writer_write(lk_text_stream_writer_t* writer, const char* text,
                                                size_t text_size);
LKC_API lk_status_t lk_text_stream_writer_close(lk_text_stream_writer_t* writer);
LKC_API lk_status_t lk_text_stream_writer_cancel(lk_text_stream_writer_t* writer,
                                                 const char* reason);
LKC_API size_t lk_text_stream_writer_id(const lk_text_stream_writer_t* writer, char* buffer,
                                        size_t buffer_size);
LKC_API int lk_text_stream_writer_is_closed(const lk_text_stream_writer_t* writer);
LKC_API void lk_text_stream_writer_destroy(lk_text_stream_writer_t* writer);
LKC_API lk_status_t lk_room_stream_bytes(lk_room_t* room, const lk_stream_bytes_options_t* options,
                                         lk_byte_stream_writer_t** writer);
LKC_API lk_status_t lk_byte_stream_writer_write(lk_byte_stream_writer_t* writer,
                                                const uint8_t* data, size_t data_size);
LKC_API lk_status_t lk_byte_stream_writer_close(lk_byte_stream_writer_t* writer);
LKC_API lk_status_t lk_byte_stream_writer_cancel(lk_byte_stream_writer_t* writer,
                                                 const char* reason);
LKC_API size_t lk_byte_stream_writer_id(const lk_byte_stream_writer_t* writer, char* buffer,
                                        size_t buffer_size);
LKC_API int lk_byte_stream_writer_is_closed(const lk_byte_stream_writer_t* writer);
LKC_API void lk_byte_stream_writer_destroy(lk_byte_stream_writer_t* writer);
LKC_API lk_status_t lk_room_register_text_stream_handler(lk_room_t* room, const char* topic,
                                                         lk_text_stream_handler handler,
                                                         void* user_data);
LKC_API lk_status_t lk_room_unregister_text_stream_handler(lk_room_t* room, const char* topic);
LKC_API lk_status_t lk_room_register_byte_stream_handler(lk_room_t* room, const char* topic,
                                                         lk_byte_stream_handler handler,
                                                         void* user_data);
LKC_API lk_status_t lk_room_unregister_byte_stream_handler(lk_room_t* room, const char* topic);

LKC_API lk_status_t lk_room_register_rpc_method(lk_room_t* room, const char* method,
                                                lk_rpc_handler handler, void* user_data);
LKC_API lk_status_t lk_room_unregister_rpc_method(lk_room_t* room, const char* method);
LKC_API lk_status_t lk_room_perform_rpc(lk_room_t* room, const lk_rpc_perform_options_t* options,
                                        lk_rpc_result_t** result);
LKC_API lk_status_t lk_room_perform_rpc_async(lk_room_t* room,
                                              const lk_rpc_perform_options_t* options,
                                              lk_rpc_completion_callback callback, void* user_data);
LKC_API void lk_rpc_result_destroy(lk_rpc_result_t* result);
LKC_API int lk_rpc_result_ok(const lk_rpc_result_t* result);
LKC_API size_t lk_rpc_result_payload(const lk_rpc_result_t* result, char* buffer,
                                     size_t buffer_size);
LKC_API uint32_t lk_rpc_result_error_code(const lk_rpc_result_t* result);
LKC_API size_t lk_rpc_result_error_message(const lk_rpc_result_t* result, char* buffer,
                                           size_t buffer_size);
LKC_API size_t lk_rpc_result_error_data(const lk_rpc_result_t* result, char* buffer,
                                        size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif
