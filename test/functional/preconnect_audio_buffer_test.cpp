#include "preconnect_audio_buffer.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstring>
#include <vector>

namespace livekit::core::detail {
namespace {

std::vector<int16_t> Samples(const std::vector<uint8_t>& bytes) {
	std::vector<int16_t> result(bytes.size() / sizeof(int16_t));
	std::memcpy(result.data(), bytes.data(), result.size() * sizeof(int16_t));
	return result;
}

TEST(PreconnectAudioBufferTest, RetainsNewestPcmWithinByteLimit) {
	PreconnectAudioBuffer buffer(8, std::chrono::seconds(10));
	std::array<int16_t, 3> first{1, 2, 3};
	std::array<int16_t, 3> second{4, 5, 6};
	buffer.on_data(first.data(), 16, 48000, 1, first.size());
	buffer.on_data(second.data(), 16, 48000, 1, second.size());

	auto data = buffer.Take();
	ASSERT_TRUE(data.has_value());
	EXPECT_EQ(Samples(data->bytes), (std::vector<int16_t>{3, 4, 5, 6}));
	EXPECT_EQ(data->sample_rate, 48000u);
	EXPECT_EQ(data->channels, 1u);
	EXPECT_EQ(data->dropped_bytes, 4u);
	EXPECT_FALSE(buffer.IsRecording());
	EXPECT_FALSE(buffer.Take().has_value());
}

TEST(PreconnectAudioBufferTest, KeepsFormatStableAndRejectsInvalidFrames) {
	PreconnectAudioBuffer buffer(64, std::chrono::seconds(10));
	std::array<int16_t, 2> samples{7, 8};
	buffer.on_data(nullptr, 16, 48000, 1, samples.size());
	buffer.on_data(samples.data(), 8, 48000, 1, samples.size());
	EXPECT_EQ(buffer.Size(), 0u);

	buffer.on_data(samples.data(), 16, 24000, 2, 1);
	buffer.on_data(samples.data(), 16, 48000, 1, samples.size());
	auto data = buffer.Take();
	ASSERT_TRUE(data.has_value());
	EXPECT_EQ(data->sample_rate, 24000u);
	EXPECT_EQ(data->channels, 2u);
	EXPECT_EQ(Samples(data->bytes), (std::vector<int16_t>{7, 8}));
}

TEST(PreconnectAudioBufferTest, DiscardsImmediatelyWhenTimeoutIsNotPositive) {
	PreconnectAudioBuffer buffer(64, std::chrono::steady_clock::duration::zero());
	std::array<int16_t, 2> samples{1, 2};
	buffer.on_data(samples.data(), 16, 48000, 1, samples.size());
	EXPECT_EQ(buffer.Size(), 0u);
	EXPECT_FALSE(buffer.IsRecording());
	EXPECT_FALSE(buffer.Take().has_value());
}

TEST(PreconnectAudioBufferTest, NotifiesWhenFirstAudioBecomesAvailable) {
	int notifications = 0;
	PreconnectAudioBuffer buffer(64, std::chrono::seconds(10), [&] { ++notifications; });
	std::array<int16_t, 2> samples{1, 2};
	buffer.on_data(samples.data(), 16, 48000, 1, samples.size());
	buffer.on_data(samples.data(), 16, 48000, 1, samples.size());
	EXPECT_EQ(notifications, 1);
}

} // namespace
} // namespace livekit::core::detail
