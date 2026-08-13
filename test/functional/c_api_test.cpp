#include "livekit/capi/livekit.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

namespace {

lk_rpc_handler_result_t EchoRpc(void*, const lk_rpc_invocation_t* invocation) {
	return {invocation->payload, 0, nullptr, nullptr};
}

void TextStream(void*, lk_room_t*, const lk_text_stream_event_t*) {}
void ByteStream(void*, lk_room_t*, const lk_byte_stream_event_t*) {}

TEST(CApiTest, ExposesVersionAndOptionDefaults) {
	const auto required = lk_version(nullptr, 0);
	ASSERT_GT(required, 1u);
	std::vector<char> version(required);
	EXPECT_EQ(lk_version(version.data(), version.size()), required);
	EXPECT_STREQ(version.data(), "0.0.1");

	std::array<char, 3> truncated{};
	EXPECT_EQ(lk_version(truncated.data(), truncated.size()), required);
	EXPECT_STREQ(truncated.data(), "0.");

	lk_audio_source_options_t audio;
	lk_audio_source_options_init(&audio);
	EXPECT_EQ(audio.struct_size, sizeof(audio));
	EXPECT_EQ(audio.sample_rate, 48000u);
	EXPECT_EQ(audio.num_channels, 1u);
	EXPECT_EQ(audio.queue_size_ms, 200u);

	lk_track_publish_options_t publish;
	lk_track_publish_options_init(&publish);
	EXPECT_EQ(publish.struct_size, sizeof(publish));
	EXPECT_EQ(publish.dtx, 1);
	EXPECT_EQ(publish.red, 1);
	EXPECT_EQ(publish.simulcast, 1);
	EXPECT_EQ(publish.video_codec, LK_VIDEO_CODEC_VP8);

	lk_file_send_options_t file;
	lk_file_send_options_init(&file);
	EXPECT_EQ(file.struct_size, sizeof(file));
	EXPECT_STREQ(file.topic, "files");
	EXPECT_STREQ(file.mime_type, "application/octet-stream");
	EXPECT_EQ(file.chunk_size, 15000u);

	lk_text_send_options_t text;
	lk_text_send_options_init(&text);
	EXPECT_EQ(text.struct_size, sizeof(text));
	EXPECT_EQ(text.chunk_size, 15000u);

	lk_byte_send_options_t bytes;
	lk_byte_send_options_init(&bytes);
	EXPECT_EQ(bytes.struct_size, sizeof(bytes));
	EXPECT_STREQ(bytes.mime_type, "application/octet-stream");
	EXPECT_EQ(bytes.chunk_size, 15000u);

	lk_stream_text_options_t stream_text;
	lk_stream_text_options_init(&stream_text);
	EXPECT_EQ(stream_text.struct_size, sizeof(stream_text));
	EXPECT_EQ(stream_text.chunk_size, 15000u);

	lk_stream_bytes_options_t stream_bytes;
	lk_stream_bytes_options_init(&stream_bytes);
	EXPECT_EQ(stream_bytes.struct_size, sizeof(stream_bytes));
	EXPECT_STREQ(stream_bytes.mime_type, "application/octet-stream");
	EXPECT_STREQ(stream_bytes.name, "unknown");
	EXPECT_EQ(stream_bytes.chunk_size, 15000u);

	lk_rpc_perform_options_t rpc;
	lk_rpc_perform_options_init(&rpc);
	EXPECT_EQ(rpc.struct_size, sizeof(rpc));
	EXPECT_EQ(rpc.response_timeout_ms, 15000u);

	lk_participant_track_permission_t permission;
	lk_participant_track_permission_init(&permission);
	EXPECT_EQ(permission.struct_size, sizeof(permission));
	EXPECT_EQ(permission.allow_all, 0);

	lk_remote_track_settings_t remote_settings;
	lk_remote_track_settings_init(&remote_settings);
	EXPECT_EQ(remote_settings.struct_size, sizeof(remote_settings));
	EXPECT_EQ(remote_settings.enabled, 1);
	EXPECT_EQ(remote_settings.has_video_quality, 0);
}

TEST(CApiTest, ValidatesArgumentsWithoutThrowingAcrossAbi) {
	EXPECT_EQ(lk_room_create(nullptr), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_NE(std::strlen(lk_last_error()), 0u);
	EXPECT_EQ(lk_room_connect(nullptr, nullptr, nullptr), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_audio_source_capture_frame(nullptr, nullptr, 0), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_video_source_capture_i420(nullptr, nullptr, 0, 0, 0, 0),
	          LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_local_track_unpublish(nullptr, 1), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_room_republish_all_tracks(nullptr), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_room_send_text(nullptr, nullptr, nullptr), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_room_send_bytes(nullptr, nullptr, 0, nullptr), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_room_stream_text(nullptr, nullptr, nullptr), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_text_stream_writer_write(nullptr, nullptr, 0), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_text_stream_writer_id(nullptr, nullptr, 0), 0u);
	EXPECT_FALSE(lk_text_stream_writer_is_closed(nullptr));
	EXPECT_EQ(lk_room_stream_bytes(nullptr, nullptr, nullptr), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_byte_stream_writer_write(nullptr, nullptr, 0), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_byte_stream_writer_id(nullptr, nullptr, 0), 0u);
	EXPECT_FALSE(lk_byte_stream_writer_is_closed(nullptr));
	EXPECT_EQ(lk_room_register_text_stream_handler(nullptr, nullptr, nullptr, nullptr),
	          LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_room_register_rpc_method(nullptr, nullptr, nullptr, nullptr),
	          LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_room_perform_rpc(nullptr, nullptr, nullptr), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_room_set_track_subscription_permissions(nullptr, 1, nullptr, 0),
	          LK_STATUS_INVALID_ARGUMENT);
}

TEST(CApiTest, CreatesRoomAndCapturesLocalFrames) {
	ASSERT_EQ(lk_init(), LK_STATUS_OK) << lk_last_error();

	lk_room_t* room = nullptr;
	ASSERT_EQ(lk_room_create(&room), LK_STATUS_OK) << lk_last_error();
	ASSERT_NE(room, nullptr);
	EXPECT_EQ(lk_room_state(room), LK_ROOM_STATE_DISCONNECTED);
	EXPECT_EQ(lk_room_disconnect_reason(room), LK_DISCONNECT_REASON_UNKNOWN);
	EXPECT_FALSE(lk_room_is_connected(room));
	EXPECT_EQ(lk_room_sid(room, nullptr, 0), 1u);

	lk_room_callbacks_t callbacks;
	lk_room_callbacks_init(&callbacks);
	EXPECT_EQ(callbacks.on_reconnecting, nullptr);
	EXPECT_EQ(callbacks.on_reconnected, nullptr);
	EXPECT_EQ(callbacks.on_disconnected_with_reason, nullptr);
	EXPECT_EQ(callbacks.on_local_track_published, nullptr);
	EXPECT_EQ(callbacks.on_local_track_unpublished, nullptr);
	EXPECT_EQ(callbacks.on_track_subscription_failed, nullptr);
	EXPECT_EQ(callbacks.on_track_unsubscribed, nullptr);
	EXPECT_EQ(callbacks.on_track_stream_state_changed, nullptr);
	EXPECT_EQ(callbacks.on_track_subscription_status_changed, nullptr);
	EXPECT_EQ(callbacks.on_data_channel_buffer_status_changed, nullptr);
	EXPECT_EQ(callbacks.on_sip_dtmf_received, nullptr);
	EXPECT_EQ(callbacks.on_chat_message_received, nullptr);
	EXPECT_EQ(callbacks.on_transcription_received, nullptr);
	EXPECT_EQ(callbacks.on_recording_status_changed, nullptr);
	EXPECT_EQ(callbacks.on_metrics_received, nullptr);
	EXPECT_EQ(callbacks.on_connection_state_changed, nullptr);
	EXPECT_EQ(callbacks.on_participant_permissions_changed, nullptr);
	EXPECT_EQ(callbacks.on_local_track_subscribed, nullptr);
	EXPECT_EQ(callbacks.on_subscribed_quality_update, nullptr);
	EXPECT_EQ(lk_room_publish_dtmf(room, 0, nullptr), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_room_send_chat_message(room, nullptr, nullptr, 0, nullptr),
	          LK_STATUS_INVALID_ARGUMENT);
	char undersized_id[LK_CHAT_MESSAGE_ID_BUFFER_SIZE - 1];
	EXPECT_EQ(
	    lk_room_send_chat_message(room, "chat", undersized_id, sizeof(undersized_id), nullptr),
	    LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_room_edit_chat_message(room, nullptr, 0, "edit"), LK_STATUS_INVALID_ARGUMENT);
	const auto current_callbacks_size = callbacks.struct_size;
	callbacks.struct_size = offsetof(lk_room_callbacks_t, on_sip_dtmf_received);
	EXPECT_EQ(lk_room_set_callbacks(room, &callbacks), LK_STATUS_OK);
	callbacks.struct_size = current_callbacks_size;

	lk_remote_track_settings_t settings;
	lk_remote_track_settings_init(&settings);
	settings.video_width = 640;
	EXPECT_EQ(lk_room_update_remote_track_settings(room, "PA_remote", "TR_video", &settings),
	          LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(callbacks.on_text_received, nullptr);
	EXPECT_EQ(callbacks.on_byte_received, nullptr);
	EXPECT_EQ(lk_room_set_callbacks(room, &callbacks), LK_STATUS_OK);
	EXPECT_EQ(lk_room_set_callbacks(room, nullptr), LK_STATUS_OK);
	EXPECT_EQ(lk_room_set_remote_track_subscribed(room, "missing", "missing", 1),
	          LK_STATUS_OPERATION_FAILED);
	EXPECT_EQ(lk_room_register_rpc_method(room, "echo", EchoRpc, nullptr), LK_STATUS_OK);
	EXPECT_EQ(lk_room_register_rpc_method(room, "echo", EchoRpc, nullptr), LK_STATUS_INVALID_STATE);
	EXPECT_EQ(lk_room_unregister_rpc_method(room, "echo"), LK_STATUS_OK);
	EXPECT_EQ(lk_room_unregister_rpc_method(room, "echo"), LK_STATUS_INVALID_STATE);
	EXPECT_EQ(lk_room_register_text_stream_handler(room, "stream-text", TextStream, nullptr),
	          LK_STATUS_OK);
	EXPECT_EQ(lk_room_register_text_stream_handler(room, "stream-text", TextStream, nullptr),
	          LK_STATUS_INVALID_STATE);
	EXPECT_EQ(lk_room_unregister_text_stream_handler(room, "stream-text"), LK_STATUS_OK);
	EXPECT_EQ(lk_room_register_byte_stream_handler(room, "stream-bytes", ByteStream, nullptr),
	          LK_STATUS_OK);
	EXPECT_EQ(lk_room_unregister_byte_stream_handler(room, "stream-bytes"), LK_STATUS_OK);
	lk_text_stream_writer_t* text_writer = nullptr;
	EXPECT_EQ(lk_room_stream_text(room, nullptr, &text_writer), LK_STATUS_OPERATION_FAILED);
	EXPECT_EQ(text_writer, nullptr);
	lk_participant_track_permission_t permission;
	lk_participant_track_permission_init(&permission);
	EXPECT_EQ(lk_room_set_track_subscription_permissions(room, 0, &permission, 1),
	          LK_STATUS_INVALID_ARGUMENT);
	permission.participant_identity = "viewer";
	EXPECT_EQ(lk_room_set_track_subscription_permissions(room, 0, &permission, 1), LK_STATUS_OK);
	EXPECT_EQ(lk_room_set_track_subscription_permissions(room, 1, nullptr, 0), LK_STATUS_OK);

	lk_audio_source_options_t audio_options;
	lk_audio_source_options_init(&audio_options);
	lk_audio_source_t* audio = nullptr;
	ASSERT_EQ(lk_audio_source_create(&audio_options, &audio), LK_STATUS_OK) << lk_last_error();
	std::vector<int16_t> samples(480, 100);
	EXPECT_EQ(lk_audio_source_capture_frame(audio, samples.data(), 480), LK_STATUS_OK)
	    << lk_last_error();
	EXPECT_EQ(lk_audio_source_destroy(audio), LK_STATUS_OK);

	lk_video_source_options_t video_options;
	lk_video_source_options_init(&video_options);
	lk_video_source_t* video = nullptr;
	ASSERT_EQ(lk_video_source_create(&video_options, &video), LK_STATUS_OK) << lk_last_error();
	std::vector<uint8_t> i420(12, 128);
	EXPECT_EQ(lk_video_source_capture_i420(video, i420.data(), i420.size(), 4, 2, 123),
	          LK_STATUS_OK)
	    << lk_last_error();
	i420.pop_back();
	EXPECT_EQ(lk_video_source_capture_i420(video, i420.data(), i420.size(), 4, 2, 124),
	          LK_STATUS_OPERATION_FAILED);
	EXPECT_EQ(lk_video_source_destroy(video), LK_STATUS_OK);

	lk_room_destroy(room);
	EXPECT_EQ(lk_shutdown(), LK_STATUS_OK) << lk_last_error();
}

} // namespace
