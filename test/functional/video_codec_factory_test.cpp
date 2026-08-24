#include "api/video_codecs/video_decoder_factory_template_open_h264_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_open_h264_adapter.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string_view>

namespace livekit::core {
namespace {

template <typename Formats> bool HasH264Format(const Formats& formats) {
	return std::any_of(formats.begin(), formats.end(),
	                   [](const auto& format) { return std::string_view(format.name) == "H264"; });
}

TEST(VideoCodecFactoryTest, H264AdaptersMatchConfiguredLibwebrtcCapability) {
	const auto encoder_formats = webrtc::OpenH264EncoderTemplateAdapter::SupportedFormats();
	const auto decoder_formats = webrtc::OpenH264DecoderTemplateAdapter::SupportedFormats();

#if defined(WEBRTC_USE_H264)
	EXPECT_TRUE(HasH264Format(encoder_formats));
	EXPECT_TRUE(HasH264Format(decoder_formats));
#else
	EXPECT_FALSE(HasH264Format(encoder_formats));
	EXPECT_FALSE(HasH264Format(decoder_formats));
#endif
}

} // namespace
} // namespace livekit::core
