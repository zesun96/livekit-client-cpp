#include "audio_gain.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>

namespace livekit::capture {
namespace {

TEST(AudioGainTest, AppliesNormalizedGainWithoutChangingShape) {
	const std::array<std::int16_t, 5> input{-32768, -1000, 0, 1000, 32767};
	std::array<std::int16_t, 5> output{};
	ASSERT_TRUE(ApplyAudioGain(input, output, 0.5F));
	EXPECT_EQ(output, (std::array<std::int16_t, 5>{-16384, -500, 0, 500, 16384}));
}

TEST(AudioGainTest, SupportsSilenceAndInPlaceUnityGain) {
	std::array<std::int16_t, 3> samples{-123, 0, 456};
	ASSERT_TRUE(ApplyAudioGain(samples, samples, 1.0F));
	EXPECT_EQ(samples, (std::array<std::int16_t, 3>{-123, 0, 456}));
	ASSERT_TRUE(ApplyAudioGain(samples, samples, 0.0F));
	EXPECT_EQ(samples, (std::array<std::int16_t, 3>{0, 0, 0}));
}

TEST(AudioGainTest, RejectsInvalidGainAndMismatchedBuffers) {
	const std::array<std::int16_t, 2> input{1, 2};
	std::array<std::int16_t, 1> output{};
	EXPECT_FALSE(ApplyAudioGain(input, output, 0.5F));
	EXPECT_FALSE(IsAudioGainValid(-0.01F));
	EXPECT_FALSE(IsAudioGainValid(1.01F));
	EXPECT_FALSE(IsAudioGainValid(std::nanf("")));
}

} // namespace
} // namespace livekit::capture
