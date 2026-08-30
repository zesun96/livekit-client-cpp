#include "public_video_frame_converter.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace livekit::core::detail {
namespace {

VideoFrame PackedFrame(VideoBufferType format, const std::vector<std::uint8_t>& pixel) {
	VideoFrame frame;
	frame.width = 2;
	frame.height = 2;
	frame.format = format;
	for (int index = 0; index < 4; ++index) {
		frame.data.insert(frame.data.end(), pixel.begin(), pixel.end());
	}
	return frame;
}

TEST(VideoFrameFormatTest, TreatsPackedChannelNamesAsMemoryOrder) {
	std::vector<std::uint8_t> expected;
	ASSERT_TRUE(
	    ConvertVideoFrameToI420(PackedFrame(VideoBufferType::RGBA, {10, 70, 200, 255}), expected));

	const std::array<VideoFrame, 4> equivalent = {
	    PackedFrame(VideoBufferType::ABGR, {255, 200, 70, 10}),
	    PackedFrame(VideoBufferType::ARGB, {255, 10, 70, 200}),
	    PackedFrame(VideoBufferType::BGRA, {200, 70, 10, 255}),
	    PackedFrame(VideoBufferType::RGB24, {10, 70, 200}),
	};
	for (const auto& frame : equivalent) {
		std::vector<std::uint8_t> converted;
		ASSERT_TRUE(ConvertVideoFrameToI420(frame, converted));
		EXPECT_EQ(converted, expected);
	}
}

TEST(VideoFrameFormatTest, ReadsExplicitPaddedPlanes) {
	VideoFrame frame;
	frame.width = 4;
	frame.height = 2;
	frame.format = VideoBufferType::I420;
	frame.data.resize(20, 0);
	std::copy_n(std::array<std::uint8_t, 4>{1, 2, 3, 4}.begin(), 4, frame.data.begin());
	std::copy_n(std::array<std::uint8_t, 4>{5, 6, 7, 8}.begin(), 4, frame.data.begin() + 6);
	frame.data[12] = 9;
	frame.data[13] = 10;
	frame.data[16] = 11;
	frame.data[17] = 12;
	frame.planes = {{0, 10, 6}, {12, 2, 4}, {16, 2, 4}};

	std::vector<std::uint8_t> converted;
	ASSERT_TRUE(ConvertVideoFrameToI420(frame, converted));
	EXPECT_EQ(converted, (std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}));
}

TEST(VideoFrameFormatTest, SupportsEveryPublicFormatAndOddDimensions) {
	const std::array<std::pair<VideoBufferType, std::size_t>, 11> formats = {{
	    {VideoBufferType::RGBA, 36},
	    {VideoBufferType::ABGR, 36},
	    {VideoBufferType::ARGB, 36},
	    {VideoBufferType::BGRA, 36},
	    {VideoBufferType::RGB24, 27},
	    {VideoBufferType::I420, 17},
	    {VideoBufferType::I420A, 26},
	    {VideoBufferType::I422, 21},
	    {VideoBufferType::I444, 27},
	    {VideoBufferType::I010, 34},
	    {VideoBufferType::NV12, 17},
	}};
	for (const auto& [format, size] : formats) {
		VideoFrame frame;
		frame.width = 3;
		frame.height = 3;
		frame.format = format;
		frame.data.resize(size, format == VideoBufferType::I010 ? 0 : 128);
		std::vector<std::uint8_t> converted;
		EXPECT_TRUE(ConvertVideoFrameToI420(frame, converted)) << static_cast<int>(format);
		EXPECT_EQ(converted.size(), 17u);
	}
}

TEST(VideoFrameFormatTest, RejectsMalformedPlaneLayouts) {
	VideoFrame frame;
	frame.width = 4;
	frame.height = 2;
	frame.format = VideoBufferType::NV12;
	frame.data.resize(16, 128);
	frame.planes = {{0, 8, 3}, {8, 4, 4}};
	std::vector<std::uint8_t> converted;
	EXPECT_FALSE(ConvertVideoFrameToI420(frame, converted));

	frame.planes = {{0, 8, 4}};
	EXPECT_FALSE(ConvertVideoFrameToI420(frame, converted));
	frame.planes = {{0, 8, 4}, {15, 4, 4}};
	EXPECT_FALSE(ConvertVideoFrameToI420(frame, converted));
}

} // namespace
} // namespace livekit::core::detail
