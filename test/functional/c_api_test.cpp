#include "livekit/capi/livekit.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

lk_rpc_handler_result_t EchoRpc(void*, const lk_rpc_invocation_t* invocation) {
	return {invocation->payload, 0, nullptr, nullptr};
}

void TextStream(void*, lk_room_t*, const lk_text_stream_event_t*) {}
void ByteStream(void*, lk_room_t*, const lk_byte_stream_event_t*) {}

struct AsyncRpcCompletion {
	std::mutex mutex;
	std::condition_variable condition;
	bool called = false;
	int ok = 0;
	uint32_t error_code = 0;
};

void RpcCompleted(void* user_data, lk_room_t*, const lk_rpc_result_t* result) {
	auto* completion = static_cast<AsyncRpcCompletion*>(user_data);
	{
		std::lock_guard<std::mutex> guard(completion->mutex);
		completion->called = true;
		completion->ok = lk_rpc_result_ok(result);
		completion->error_code = lk_rpc_result_error_code(result);
	}
	completion->condition.notify_one();
}

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
	ASSERT_NE(publish.scalability_mode, nullptr);
	EXPECT_STREQ(publish.scalability_mode, "L3T3_KEY");
	EXPECT_EQ(publish.backup_video_codec_enabled, 0);
	EXPECT_EQ(publish.backup_video_codec, LK_VIDEO_CODEC_VP8);
	EXPECT_EQ(publish.backup_codec_policy, LK_BACKUP_CODEC_POLICY_PREFER_REGRESSION);
	EXPECT_EQ(publish.video_encoding.struct_size, sizeof(publish.video_encoding));
	EXPECT_EQ(publish.video_encoding.max_bitrate, 0u);
	EXPECT_FLOAT_EQ(publish.video_encoding.max_framerate, 0.0F);
	EXPECT_EQ(publish.backup_video_encoding.struct_size, sizeof(publish.backup_video_encoding));

	lk_video_encoding_t encoding;
	lk_video_encoding_init(&encoding);
	EXPECT_EQ(encoding.struct_size, sizeof(encoding));
	EXPECT_EQ(encoding.max_bitrate, 0u);
	EXPECT_FLOAT_EQ(encoding.max_framerate, 0.0F);

	lk_e2ee_options_t e2ee;
	lk_e2ee_options_init(&e2ee);
	EXPECT_EQ(e2ee.struct_size, sizeof(e2ee));
	EXPECT_EQ(e2ee.enabled, 1);
	EXPECT_EQ(e2ee.shared_key, nullptr);
	EXPECT_EQ(e2ee.shared_key_size, 0u);
	EXPECT_EQ(e2ee.ratchet_salt, nullptr);
	EXPECT_EQ(e2ee.ratchet_window_size, 16u);
	EXPECT_EQ(e2ee.failure_tolerance, -1);
	EXPECT_EQ(e2ee.key_ring_size, 16u);
	EXPECT_EQ(e2ee.key_derivation, LK_E2EE_KEY_DERIVATION_PBKDF2_SHA256);

	lk_frame_cryptor_info_t cryptor;
	lk_frame_cryptor_info_init(&cryptor);
	EXPECT_EQ(cryptor.struct_size, sizeof(cryptor));
	EXPECT_EQ(cryptor.kind, LK_TRACK_KIND_UNKNOWN);
	EXPECT_EQ(cryptor.direction, LK_FRAME_CRYPTOR_DIRECTION_SENDER);
	EXPECT_EQ(cryptor.state, LK_FRAME_CRYPTOR_STATE_NEW);

	lk_file_send_options_t file;
	lk_file_send_options_init(&file);
	EXPECT_EQ(file.struct_size, sizeof(file));
	EXPECT_STREQ(file.topic, "files");
	EXPECT_STREQ(file.mime_type, "application/octet-stream");
	EXPECT_EQ(file.chunk_size, 15000u);
	EXPECT_EQ(file.compress, 0);

	lk_text_send_options_t text;
	lk_text_send_options_init(&text);
	EXPECT_EQ(text.struct_size, sizeof(text));
	EXPECT_EQ(text.chunk_size, 15000u);
	EXPECT_EQ(text.compress, 0);

	lk_byte_send_options_t bytes;
	lk_byte_send_options_init(&bytes);
	EXPECT_EQ(bytes.struct_size, sizeof(bytes));
	EXPECT_STREQ(bytes.mime_type, "application/octet-stream");
	EXPECT_EQ(bytes.chunk_size, 15000u);
	EXPECT_EQ(bytes.compress, 0);

	lk_stream_text_options_t stream_text;
	lk_stream_text_options_init(&stream_text);
	EXPECT_EQ(stream_text.struct_size, sizeof(stream_text));
	EXPECT_EQ(stream_text.chunk_size, 15000u);
	EXPECT_EQ(stream_text.compress, 0);
	EXPECT_EQ(stream_text.on_complete, nullptr);
	EXPECT_EQ(stream_text.completion_user_data, nullptr);

	lk_stream_bytes_options_t stream_bytes;
	lk_stream_bytes_options_init(&stream_bytes);
	EXPECT_EQ(stream_bytes.struct_size, sizeof(stream_bytes));
	EXPECT_STREQ(stream_bytes.mime_type, "application/octet-stream");
	EXPECT_STREQ(stream_bytes.name, "unknown");
	EXPECT_EQ(stream_bytes.chunk_size, 15000u);
	EXPECT_EQ(stream_bytes.compress, 0);
	EXPECT_EQ(stream_bytes.on_complete, nullptr);
	EXPECT_EQ(stream_bytes.completion_user_data, nullptr);

	lk_rpc_perform_options_t rpc;
	lk_rpc_perform_options_init(&rpc);
	EXPECT_EQ(rpc.struct_size, sizeof(rpc));
	EXPECT_EQ(rpc.response_timeout_ms, 15000u);

	lk_camera_capture_options_t camera;
	lk_camera_capture_options_init(&camera);
	EXPECT_EQ(camera.struct_size, sizeof(camera));
	EXPECT_EQ(camera.device_id, nullptr);
	EXPECT_EQ(camera.width, 1280u);
	EXPECT_EQ(camera.height, 720u);
	EXPECT_EQ(camera.frames_per_second, 30u);

	lk_screen_capture_options_t screen;
	lk_screen_capture_options_init(&screen);
	EXPECT_EQ(screen.struct_size, sizeof(screen));
	EXPECT_EQ(screen.source_id, nullptr);
	EXPECT_EQ(screen.frames_per_second, 15u);
	EXPECT_EQ(screen.include_cursor, 1);

	lk_microphone_capture_options_t microphone;
	lk_microphone_capture_options_init(&microphone);
	EXPECT_EQ(microphone.struct_size, sizeof(microphone));
	EXPECT_EQ(microphone.device_id, nullptr);
	EXPECT_EQ(microphone.queue_size_ms, 200u);
	EXPECT_EQ(microphone.echo_cancellation, 1);
	EXPECT_EQ(microphone.auto_gain_control, 1);
	EXPECT_EQ(microphone.noise_suppression, 1);
	lk_system_audio_capture_options_t system_audio;
	lk_system_audio_capture_options_init(&system_audio);
	EXPECT_EQ(system_audio.struct_size, sizeof(system_audio));
	EXPECT_EQ(system_audio.device_id, nullptr);
	EXPECT_EQ(system_audio.queue_size_ms, 200u);
	lk_microphone_processing_stats_t microphone_stats;
	lk_microphone_processing_stats_init(&microphone_stats);
	EXPECT_EQ(microphone_stats.struct_size, sizeof(microphone_stats));
	EXPECT_EQ(microphone_stats.capture_frames_processed, 0u);
	EXPECT_EQ(microphone_stats.render_frames_processed, 0u);
	lk_audio_playback_stats_t playback_stats;
	lk_audio_playback_stats_init(&playback_stats);
	EXPECT_EQ(playback_stats.struct_size, sizeof(playback_stats));
	EXPECT_EQ(playback_stats.queued_frames, 0u);
	EXPECT_EQ(playback_stats.played_frames, 0u);
	EXPECT_EQ(playback_stats.dropped_frames, 0u);
	EXPECT_EQ(playback_stats.underrun_frames, 0u);
	EXPECT_EQ(playback_stats.buffered_duration_ms, 0u);
	EXPECT_EQ(playback_stats.device_latency_ms, 0u);
	EXPECT_EQ(playback_stats.estimated_delay_ms, 0u);

	lk_participant_track_permission_t permission;
	lk_participant_track_permission_init(&permission);
	EXPECT_EQ(permission.struct_size, sizeof(permission));
	EXPECT_EQ(permission.allow_all, 0);

	lk_remote_track_settings_t remote_settings;
	lk_remote_track_settings_init(&remote_settings);
	EXPECT_EQ(remote_settings.struct_size, sizeof(remote_settings));
	EXPECT_EQ(remote_settings.enabled, 1);
	EXPECT_EQ(remote_settings.has_video_quality, 0);

	lk_remote_participant_snapshot_info_t participant_snapshot_info;
	lk_remote_participant_snapshot_info_init(&participant_snapshot_info);
	EXPECT_EQ(participant_snapshot_info.struct_size, sizeof(participant_snapshot_info));
	lk_remote_track_publication_snapshot_info_t publication_snapshot_info;
	lk_remote_track_publication_snapshot_info_init(&publication_snapshot_info);
	EXPECT_EQ(publication_snapshot_info.struct_size, sizeof(publication_snapshot_info));
	lk_remote_track_snapshot_info_t track_snapshot_info;
	lk_remote_track_snapshot_info_init(&track_snapshot_info);
	EXPECT_EQ(track_snapshot_info.struct_size, sizeof(track_snapshot_info));
}

TEST(CApiTest, OwnsMediaDeviceSnapshotAndValidatesIndexes) {
	lk_media_device_list_t* devices = nullptr;
	ASSERT_EQ(lk_media_device_list_create(&devices), LK_STATUS_OK);
	ASSERT_NE(devices, nullptr);
	const auto count = lk_media_device_list_count(devices);
	for (size_t index = 0; index < count; ++index) {
		lk_media_device_info_t info{};
		info.struct_size = sizeof(info);
		EXPECT_EQ(lk_media_device_list_info(devices, index, &info), LK_STATUS_OK);
		EXPECT_GE(info.kind, LK_MEDIA_DEVICE_KIND_AUDIO_INPUT);
		EXPECT_LE(info.kind, LK_MEDIA_DEVICE_KIND_VIDEO_INPUT);

		const auto id_size = lk_media_device_list_id(devices, index, nullptr, 0);
		const auto label_size = lk_media_device_list_label(devices, index, nullptr, 0);
		ASSERT_GT(id_size, 1u);
		ASSERT_GT(label_size, 1u);
		std::vector<char> id(id_size);
		std::vector<char> label(label_size);
		EXPECT_EQ(lk_media_device_list_id(devices, index, id.data(), id.size()), id_size);
		EXPECT_EQ(lk_media_device_list_label(devices, index, label.data(), label.size()),
		          label_size);
	}

	lk_media_device_info_t invalid_info{};
	invalid_info.struct_size = sizeof(invalid_info);
	EXPECT_EQ(lk_media_device_list_info(devices, count, &invalid_info), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_NE(std::strlen(lk_last_error()), 0u);
	lk_media_device_list_destroy(devices);
	EXPECT_EQ(lk_media_device_list_create(nullptr), LK_STATUS_INVALID_ARGUMENT);
}

TEST(CApiTest, OwnsScreenSourceSnapshotAndValidatesIndexes) {
	lk_screen_source_list_t* sources = nullptr;
	ASSERT_EQ(lk_screen_source_list_create(&sources), LK_STATUS_OK);
	ASSERT_NE(sources, nullptr);
	const auto count = lk_screen_source_list_count(sources);
	for (size_t index = 0; index < count; ++index) {
		lk_screen_source_info_t info{};
		info.struct_size = sizeof(info);
		EXPECT_EQ(lk_screen_source_list_info(sources, index, &info), LK_STATUS_OK);
		EXPECT_GE(info.kind, LK_SCREEN_SOURCE_KIND_MONITOR);
		EXPECT_LE(info.kind, LK_SCREEN_SOURCE_KIND_WINDOW);
		EXPECT_GT(info.width, 0U);
		EXPECT_GT(info.height, 0U);

		const auto id_size = lk_screen_source_list_id(sources, index, nullptr, 0);
		const auto label_size = lk_screen_source_list_label(sources, index, nullptr, 0);
		ASSERT_GT(id_size, 1U);
		ASSERT_GT(label_size, 1U);
		std::vector<char> id(id_size);
		std::vector<char> label(label_size);
		EXPECT_EQ(lk_screen_source_list_id(sources, index, id.data(), id.size()), id_size);
		EXPECT_EQ(lk_screen_source_list_label(sources, index, label.data(), label.size()),
		          label_size);
	}

	lk_screen_source_info_t invalid_info{};
	invalid_info.struct_size = sizeof(invalid_info);
	EXPECT_EQ(lk_screen_source_list_info(sources, count, &invalid_info),
	          LK_STATUS_INVALID_ARGUMENT);
	lk_screen_source_list_destroy(sources);
	EXPECT_EQ(lk_screen_source_list_create(nullptr), LK_STATUS_INVALID_ARGUMENT);
}

TEST(CApiTest, RejectsCameraOperationsForExternalVideoSource) {
	lk_video_source_t* source = nullptr;
	ASSERT_EQ(lk_video_source_create(nullptr, &source), LK_STATUS_OK);
	ASSERT_NE(source, nullptr);
	EXPECT_EQ(lk_video_source_camera_start(source), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_video_source_camera_stop(source), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_video_source_camera_is_capturing(source), 0);
	EXPECT_EQ(lk_video_source_camera_device_id(source, nullptr, 0), 0u);
	EXPECT_EQ(lk_video_source_camera_switch_device(source, "missing"), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_video_source_screen_start(source), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_video_source_screen_stop(source), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_video_source_screen_is_capturing(source), 0);
	EXPECT_EQ(lk_video_source_screen_source_id(source, nullptr, 0), 0U);
	EXPECT_EQ(lk_video_source_screen_switch_source(source, "missing"), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_video_source_destroy(source), LK_STATUS_OK);

	lk_camera_capture_options_t options;
	lk_camera_capture_options_init(&options);
	options.width = 0;
	EXPECT_EQ(lk_video_source_create_camera(&options, &source), LK_STATUS_INVALID_ARGUMENT);

	lk_screen_capture_options_t screen_options;
	lk_screen_capture_options_init(&screen_options);
	EXPECT_EQ(lk_video_source_create_screen(&screen_options, &source), LK_STATUS_INVALID_ARGUMENT);
	screen_options.source_id = "monitor:missing";
	EXPECT_EQ(lk_video_source_create_screen(&screen_options, &source), LK_STATUS_OPERATION_FAILED);
}

TEST(CApiTest, RejectsMicrophoneOperationsForExternalAudioSource) {
	lk_audio_source_t* source = nullptr;
	ASSERT_EQ(lk_audio_source_create(nullptr, &source), LK_STATUS_OK);
	ASSERT_NE(source, nullptr);
	EXPECT_EQ(lk_audio_source_microphone_start(source), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_audio_source_microphone_stop(source), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_audio_source_microphone_is_capturing(source), 0);
	EXPECT_EQ(lk_audio_source_microphone_device_id(source, nullptr, 0), 0u);
	EXPECT_EQ(lk_audio_source_microphone_switch_device(source, "missing"),
	          LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_audio_source_microphone_set_muted(source, 1), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_audio_source_microphone_is_muted(source), 0);
	EXPECT_EQ(lk_audio_source_microphone_set_volume(source, 0.5F), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_FLOAT_EQ(lk_audio_source_microphone_volume(source), 0.0F);
	lk_audio_source_options_t processing_options;
	lk_audio_source_options_init(&processing_options);
	EXPECT_EQ(lk_audio_source_microphone_set_processing_options(source, &processing_options),
	          LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_audio_source_microphone_processing_options(source, &processing_options),
	          LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_audio_source_system_audio_start(source), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_audio_source_system_audio_stop(source), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_FALSE(lk_audio_source_system_audio_is_capturing(source));
	EXPECT_EQ(lk_audio_source_system_audio_device_id(source, nullptr, 0), 0u);
	EXPECT_EQ(lk_audio_source_system_audio_switch_device(source, "missing"),
	          LK_STATUS_INVALID_ARGUMENT);
	lk_microphone_processing_stats_t stats;
	lk_microphone_processing_stats_init(&stats);
	EXPECT_EQ(lk_audio_source_microphone_processing_stats(source, &stats),
	          LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_audio_source_microphone_processing_stats(nullptr, &stats),
	          LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_audio_source_destroy(source), LK_STATUS_OK);

	lk_microphone_capture_options_t options;
	lk_microphone_capture_options_init(&options);
	options.queue_size_ms = 1;
	EXPECT_EQ(lk_audio_source_create_microphone(&options, &source), LK_STATUS_INVALID_ARGUMENT);
	lk_system_audio_capture_options_t system_audio_options;
	lk_system_audio_capture_options_init(&system_audio_options);
	system_audio_options.queue_size_ms = 1;
	EXPECT_EQ(lk_audio_source_create_system_audio(&system_audio_options, &source),
	          LK_STATUS_INVALID_ARGUMENT);
}

TEST(CApiTest, ValidatesArgumentsWithoutThrowingAcrossAbi) {
	EXPECT_EQ(lk_room_create(nullptr), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_NE(std::strlen(lk_last_error()), 0u);
	EXPECT_EQ(lk_room_connect(nullptr, nullptr, nullptr), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_room_connect_e2ee(nullptr, nullptr, nullptr, nullptr), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_audio_source_capture_frame(nullptr, nullptr, 0), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_video_source_capture_i420(nullptr, nullptr, 0, 0, 0, 0),
	          LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_local_track_unpublish(nullptr, 1), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_local_track_rtc_stats(nullptr, nullptr, 0), 0u);
	EXPECT_EQ(lk_local_track_publish_screen_share_video(nullptr, nullptr, nullptr),
	          LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_local_track_publish_screen_share_audio(nullptr, nullptr, nullptr),
	          LK_STATUS_INVALID_ARGUMENT);
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
	EXPECT_EQ(lk_room_perform_rpc_async(nullptr, nullptr, nullptr, nullptr),
	          LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_room_set_track_subscription_permissions(nullptr, 1, nullptr, 0),
	          LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_room_create_remote_participant_snapshot(nullptr, nullptr),
	          LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_room_set_audio_output_device(nullptr, nullptr), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_room_audio_output_device(nullptr, nullptr, 0), 0u);
	EXPECT_EQ(lk_room_set_speaker_volume(nullptr, 0.5F), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_FLOAT_EQ(lk_room_speaker_volume(nullptr), 0.0F);
	EXPECT_EQ(lk_room_set_speaker_muted(nullptr, 1), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_FALSE(lk_room_speaker_is_muted(nullptr));
	EXPECT_FALSE(lk_room_e2ee_is_configured(nullptr));
	EXPECT_FALSE(lk_room_e2ee_is_enabled(nullptr));
	EXPECT_EQ(lk_room_e2ee_set_enabled(nullptr, 1), LK_STATUS_INVALID_STATE);
	EXPECT_EQ(lk_room_e2ee_set_shared_key(nullptr, nullptr, 0, 0), LK_STATUS_INVALID_STATE);
	EXPECT_EQ(lk_room_e2ee_export_shared_key(nullptr, 0, nullptr, 0), 0u);
	EXPECT_EQ(lk_room_e2ee_set_data_key_index(nullptr, 0), LK_STATUS_INVALID_STATE);
	EXPECT_EQ(lk_room_e2ee_set_frame_cryptor_enabled(nullptr, nullptr,
	                                                 LK_FRAME_CRYPTOR_DIRECTION_SENDER, 1),
	          LK_STATUS_INVALID_STATE);
	EXPECT_EQ(lk_frame_cryptor_list_create(nullptr, nullptr), LK_STATUS_INVALID_ARGUMENT);
	lk_audio_playback_stats_t playback_stats;
	lk_audio_playback_stats_init(&playback_stats);
	EXPECT_EQ(lk_room_audio_playback_stats(nullptr, &playback_stats), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_remote_participant_list_at(nullptr, 0, nullptr), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_remote_participant_snapshot_info(nullptr, nullptr), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_remote_track_publication_snapshot_info(nullptr, nullptr),
	          LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_remote_track_snapshot_info(nullptr, nullptr), LK_STATUS_INVALID_ARGUMENT);
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
	EXPECT_EQ(lk_room_set_audio_output_device(room, "missing"), LK_STATUS_OPERATION_FAILED);
	lk_e2ee_options_t invalid_e2ee;
	lk_e2ee_options_init(&invalid_e2ee);
	invalid_e2ee.key_ring_size = 0;
	EXPECT_EQ(lk_room_connect_e2ee(room, "http://127.0.0.1/rtc", "token", &invalid_e2ee),
	          LK_STATUS_INVALID_ARGUMENT);
	lk_frame_cryptor_list_t* unconfigured_cryptors = nullptr;
	EXPECT_EQ(lk_frame_cryptor_list_create(room, &unconfigured_cryptors), LK_STATUS_INVALID_STATE);
	EXPECT_EQ(unconfigured_cryptors, nullptr);
	EXPECT_EQ(lk_room_set_speaker_volume(room, -0.1F), LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(lk_room_set_speaker_volume(room, 1.1F), LK_STATUS_INVALID_ARGUMENT);
	lk_audio_playback_stats_t playback_stats;
	lk_audio_playback_stats_init(&playback_stats);
	EXPECT_EQ(lk_room_audio_playback_stats(room, &playback_stats), LK_STATUS_OK);
	EXPECT_EQ(playback_stats.queued_frames, 0u);
	lk_remote_participant_list_t* participant_snapshot = nullptr;
	ASSERT_EQ(lk_room_create_remote_participant_snapshot(room, &participant_snapshot),
	          LK_STATUS_OK);
	ASSERT_NE(participant_snapshot, nullptr);
	EXPECT_EQ(lk_remote_participant_list_count(participant_snapshot), 0u);
	const lk_remote_participant_snapshot_t* missing_participant = nullptr;
	EXPECT_EQ(lk_remote_participant_list_at(participant_snapshot, 0, &missing_participant),
	          LK_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(missing_participant, nullptr);
	lk_remote_participant_list_destroy(participant_snapshot);

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
	EXPECT_EQ(callbacks.on_encryption_state_changed, nullptr);
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
	lk_rpc_perform_options_t async_rpc_options;
	lk_rpc_perform_options_init(&async_rpc_options);
	async_rpc_options.destination_identity = "missing-participant";
	async_rpc_options.method = "missing-method";
	AsyncRpcCompletion async_rpc_completion;
	ASSERT_EQ(
	    lk_room_perform_rpc_async(room, &async_rpc_options, RpcCompleted, &async_rpc_completion),
	    LK_STATUS_OK);
	{
		std::unique_lock<std::mutex> lock(async_rpc_completion.mutex);
		ASSERT_TRUE(async_rpc_completion.condition.wait_for(
		    lock, std::chrono::seconds(2), [&] { return async_rpc_completion.called; }));
	}
	EXPECT_FALSE(async_rpc_completion.ok);
	EXPECT_EQ(async_rpc_completion.error_code,
	          static_cast<uint32_t>(LK_RPC_ERROR_RECIPIENT_NOT_FOUND));
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
