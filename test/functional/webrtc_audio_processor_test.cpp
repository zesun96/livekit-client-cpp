#include "webrtc_audio_processor.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace livekit::capture {
namespace {

TEST(WebRtcAudioProcessorTest, AcceptsTenMillisecondCaptureAndRenderFrames) {
	WebRtcAudioProcessor processor(true, true, true);
	std::array<std::int16_t, 480> capture{};
	const std::array<std::int16_t, 960> render{};

	EXPECT_TRUE(processor.ProcessRender(render, 48000, 2));
	EXPECT_TRUE(processor.ProcessCapture(capture, 48000, 1));
	EXPECT_FALSE(processor.ProcessCapture(std::span<std::int16_t>(capture).first(479), 48000, 1));
	EXPECT_FALSE(
	    processor.ProcessRender(std::span<const std::int16_t>(render).first(959), 48000, 2));
}

TEST(WebRtcAudioProcessorTest, PreservesPcmWhenAllProcessingIsDisabled) {
	WebRtcAudioProcessor processor(false, false, false);
	std::array<std::int16_t, 480> capture{};
	capture[0] = -1234;
	capture[479] = 2345;
	const auto expected = capture;
	const std::array<std::int16_t, 480> render{};

	ASSERT_TRUE(processor.ProcessCapture(capture, 48000, 1));
	EXPECT_EQ(capture, expected);
	EXPECT_FALSE(processor.ProcessRender(render, 48000, 1));
}

} // namespace
} // namespace livekit::capture
