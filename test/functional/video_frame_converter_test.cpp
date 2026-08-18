#include "video_frame_converter.h"

#include <gtest/gtest.h>

#include <array>

namespace livekit::capture {
namespace {

TEST(VideoFrameConverterTest, ConvertsBgraFramesToTightlyPackedI420) {
	const std::array<std::uint8_t, 24> pixels{
	    0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255,
	};
	CapturedVideoFrame frame;
	ASSERT_TRUE(ConvertBgraToI420(pixels.data(), 3, 2, 12, 1234, frame));
	EXPECT_EQ(frame.width, 2U);
	EXPECT_EQ(frame.height, 2U);
	EXPECT_EQ(frame.timestamp_us, 1234);
	ASSERT_EQ(frame.i420.size(), 6U);
	EXPECT_EQ(frame.i420[0], 16U);
	EXPECT_EQ(frame.i420[4], 128U);
	EXPECT_EQ(frame.i420[5], 128U);
}

TEST(VideoFrameConverterTest, RejectsInvalidBgraFrames) {
	CapturedVideoFrame frame;
	EXPECT_FALSE(ConvertBgraToI420(nullptr, 2, 2, 8, 0, frame));
	const std::array<std::uint8_t, 16> pixels{};
	EXPECT_FALSE(ConvertBgraToI420(pixels.data(), 2, 2, 7, 0, frame));
	EXPECT_FALSE(ConvertBgraToI420(pixels.data(), 1, 1, 4, 0, frame));
}

} // namespace
} // namespace livekit::capture
