#include "livekit/capi/livekit.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <vector>

namespace {

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

	lk_file_send_options_t file;
	lk_file_send_options_init(&file);
	EXPECT_EQ(file.struct_size, sizeof(file));
	EXPECT_STREQ(file.topic, "files");
	EXPECT_STREQ(file.mime_type, "application/octet-stream");
	EXPECT_EQ(file.chunk_size, 15000u);
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
}

TEST(CApiTest, CreatesRoomAndCapturesLocalFrames) {
	ASSERT_EQ(lk_init(), LK_STATUS_OK) << lk_last_error();

	lk_room_t* room = nullptr;
	ASSERT_EQ(lk_room_create(&room), LK_STATUS_OK) << lk_last_error();
	ASSERT_NE(room, nullptr);
	EXPECT_EQ(lk_room_state(room), LK_ROOM_STATE_DISCONNECTED);
	EXPECT_FALSE(lk_room_is_connected(room));
	EXPECT_EQ(lk_room_sid(room, nullptr, 0), 1u);

	lk_room_callbacks_t callbacks;
	lk_room_callbacks_init(&callbacks);
	EXPECT_EQ(callbacks.on_local_track_published, nullptr);
	EXPECT_EQ(callbacks.on_local_track_unpublished, nullptr);
	EXPECT_EQ(lk_room_set_callbacks(room, &callbacks), LK_STATUS_OK);
	EXPECT_EQ(lk_room_set_callbacks(room, nullptr), LK_STATUS_OK);
	EXPECT_EQ(lk_room_set_remote_track_subscribed(room, "missing", "missing", 1),
	          LK_STATUS_OPERATION_FAILED);

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
