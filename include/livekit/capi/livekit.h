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

typedef enum lk_status {
	LK_STATUS_OK = 0,
	LK_STATUS_INVALID_ARGUMENT = 1,
	LK_STATUS_INVALID_STATE = 2,
	LK_STATUS_OPERATION_FAILED = 3,
	LK_STATUS_EXCEPTION = 4
} lk_status_t;

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
	LK_VIDEO_QUALITY_HIGH = 2
} lk_video_quality_t;

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
typedef void (*lk_data_stream_progress_callback)(void* user_data, uint64_t bytes_sent,
                                                 int has_total_size, uint64_t total_size);
typedef void (*lk_text_stream_handler)(void* user_data, lk_room_t* room,
                                       const lk_text_stream_event_t* event);
typedef void (*lk_byte_stream_handler)(void* user_data, lk_room_t* room,
                                       const lk_byte_stream_event_t* event);
typedef void (*lk_data_channel_buffer_status_callback)(
    void* user_data, lk_room_t* room, const lk_data_channel_buffer_status_t* status);

typedef void (*lk_room_event_callback)(void* user_data, lk_room_t* room);
typedef void (*lk_room_disconnected_callback)(void* user_data, lk_room_t* room,
                                              lk_disconnect_reason_t reason);
typedef void (*lk_participant_event_callback)(void* user_data, lk_room_t* room,
                                              const lk_participant_info_t* participant);
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
} lk_room_callbacks_t;

typedef struct lk_audio_source_options {
	size_t struct_size;
	uint32_t sample_rate;
	uint32_t num_channels;
	uint32_t queue_size_ms;
	int echo_cancellation;
	int auto_gain_control;
	int noise_suppression;
} lk_audio_source_options_t;

typedef struct lk_video_source_options {
	size_t struct_size;
	int is_screencast;
} lk_video_source_options_t;

typedef struct lk_track_publish_options {
	size_t struct_size;
	lk_track_source_t source;
	int dtx;
	int red;
	int simulcast;
	const char* stream;
} lk_track_publish_options_t;

typedef struct lk_data_publish_options {
	size_t struct_size;
	int reliable;
	const char* topic;
	const char* const* destination_identities;
	size_t destination_identity_count;
} lk_data_publish_options_t;

typedef struct lk_file_send_options {
	size_t struct_size;
	const char* topic;
	const char* mime_type;
	const char* const* destination_identities;
	size_t destination_identity_count;
	size_t chunk_size;
	const lk_attribute_t* attributes;
	size_t attribute_count;
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

LKC_API void lk_room_callbacks_init(lk_room_callbacks_t* callbacks);
LKC_API void lk_audio_source_options_init(lk_audio_source_options_t* options);
LKC_API void lk_video_source_options_init(lk_video_source_options_t* options);
LKC_API void lk_track_publish_options_init(lk_track_publish_options_t* options);
LKC_API void lk_data_publish_options_init(lk_data_publish_options_t* options);
LKC_API void lk_file_send_options_init(lk_file_send_options_t* options);
LKC_API void lk_text_send_options_init(lk_text_send_options_t* options);
LKC_API void lk_byte_send_options_init(lk_byte_send_options_t* options);
LKC_API void lk_stream_text_options_init(lk_stream_text_options_t* options);
LKC_API void lk_stream_bytes_options_init(lk_stream_bytes_options_t* options);
LKC_API void lk_rpc_perform_options_init(lk_rpc_perform_options_t* options);
LKC_API void lk_participant_track_permission_init(lk_participant_track_permission_t* permission);
LKC_API void lk_remote_track_settings_init(lk_remote_track_settings_t* settings);

LKC_API lk_status_t lk_room_create(lk_room_t** room);
LKC_API void lk_room_destroy(lk_room_t* room);
LKC_API lk_status_t lk_room_set_callbacks(lk_room_t* room, const lk_room_callbacks_t* callbacks);
LKC_API lk_status_t lk_room_connect(lk_room_t* room, const char* url, const char* token);
LKC_API lk_status_t lk_room_disconnect(lk_room_t* room);
LKC_API lk_room_state_t lk_room_state(const lk_room_t* room);
LKC_API lk_disconnect_reason_t lk_room_disconnect_reason(const lk_room_t* room);
LKC_API int lk_room_is_connected(const lk_room_t* room);
LKC_API size_t lk_room_sid(const lk_room_t* room, char* buffer, size_t buffer_size);
LKC_API size_t lk_room_name(const lk_room_t* room, char* buffer, size_t buffer_size);
LKC_API size_t lk_room_metadata(const lk_room_t* room, char* buffer, size_t buffer_size);
LKC_API int lk_room_is_recording(const lk_room_t* room);

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
LKC_API lk_status_t lk_audio_source_destroy(lk_audio_source_t* source);
LKC_API lk_status_t lk_audio_source_capture_frame(lk_audio_source_t* source, const int16_t* data,
                                                  uint32_t samples_per_channel);
LKC_API lk_status_t lk_video_source_create(const lk_video_source_options_t* options,
                                           lk_video_source_t** source);
LKC_API lk_status_t lk_video_source_destroy(lk_video_source_t* source);
LKC_API lk_status_t lk_video_source_capture_i420(lk_video_source_t* source, const uint8_t* data,
                                                 size_t data_size, uint32_t width, uint32_t height,
                                                 int64_t timestamp_us);

LKC_API lk_status_t lk_room_create_audio_track(lk_room_t* room, const char* label,
                                               lk_audio_source_t* source, lk_local_track_t** track);
LKC_API lk_status_t lk_room_create_video_track(lk_room_t* room, const char* label,
                                               lk_video_source_t* source, lk_local_track_t** track);
LKC_API lk_status_t lk_local_track_publish(lk_room_t* room, lk_local_track_t* track,
                                           const lk_track_publish_options_t* options);
LKC_API lk_status_t lk_local_track_unpublish(lk_local_track_t* track, int stop_on_unpublish);
LKC_API lk_status_t lk_room_republish_all_tracks(lk_room_t* room);
LKC_API lk_status_t lk_local_track_set_muted(lk_local_track_t* track, int muted);
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
