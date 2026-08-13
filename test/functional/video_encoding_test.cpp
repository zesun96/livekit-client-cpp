#include "video_encoding.h"

#include <gtest/gtest.h>

namespace livekit::core {
namespace {

TEST(VideoEncodingTest, BuildsThreeOrderedLayersForHdCamera) {
	TrackPublishOptions options;
	options.simulcast = true;
	const auto plan = BuildVideoEncodingPlan(1280, 720, false, options);

	ASSERT_EQ(plan.encodings.size(), 3u);
	ASSERT_EQ(plan.layers.size(), 3u);
	EXPECT_EQ(plan.encodings[0].rid, "q");
	EXPECT_EQ(plan.encodings[1].rid, "h");
	EXPECT_EQ(plan.encodings[2].rid, "f");
	EXPECT_DOUBLE_EQ(*plan.encodings[0].scale_resolution_down_by, 4.0);
	EXPECT_DOUBLE_EQ(*plan.encodings[1].scale_resolution_down_by, 2.0);
	EXPECT_DOUBLE_EQ(*plan.encodings[2].scale_resolution_down_by, 1.0);
	EXPECT_EQ(plan.layers[0].quality(), livekit::VideoQuality::LOW);
	EXPECT_EQ(plan.layers[1].quality(), livekit::VideoQuality::MEDIUM);
	EXPECT_EQ(plan.layers[2].quality(), livekit::VideoQuality::HIGH);
	EXPECT_EQ(plan.layers[0].width(), 320u);
	EXPECT_EQ(plan.layers[1].height(), 360u);
	EXPECT_EQ(plan.layers[2].bitrate(), 1700000u);
}

TEST(VideoEncodingTest, BuildsSingleHighLayerWhenSimulcastIsDisabled) {
	TrackPublishOptions options;
	options.simulcast = false;
	options.video_encoding = {900000, 24};
	const auto plan = BuildVideoEncodingPlan(640, 360, false, options);

	ASSERT_EQ(plan.encodings.size(), 1u);
	ASSERT_EQ(plan.layers.size(), 1u);
	EXPECT_TRUE(plan.encodings[0].rid.empty());
	EXPECT_EQ(plan.encodings[0].max_bitrate_bps, 900000);
	EXPECT_EQ(plan.encodings[0].max_framerate, 24);
	EXPECT_EQ(plan.layers[0].quality(), livekit::VideoQuality::HIGH);
	EXPECT_EQ(plan.layers[0].width(), 640u);
	EXPECT_EQ(plan.layers[0].height(), 360u);
}

TEST(VideoEncodingTest, CapsLowerLayerFramerateToUserSetting) {
	TrackPublishOptions options;
	options.video_encoding.max_framerate = 15;
	const auto plan = BuildVideoEncodingPlan(1280, 720, false, options);

	ASSERT_EQ(plan.encodings.size(), 3u);
	EXPECT_EQ(plan.encodings[0].max_framerate, 15);
	EXPECT_EQ(plan.encodings[1].max_framerate, 15);
	EXPECT_EQ(plan.encodings[2].max_framerate, 15);
}

TEST(VideoEncodingTest, DoesNotInventMultipleLayersForTinyVideo) {
	TrackPublishOptions options;
	options.simulcast = true;
	const auto plan = BuildVideoEncodingPlan(320, 180, false, options);

	ASSERT_EQ(plan.encodings.size(), 1u);
	EXPECT_EQ(plan.encodings[0].rid, "q");
	EXPECT_EQ(plan.layers[0].quality(), livekit::VideoQuality::LOW);
}

TEST(VideoEncodingTest, BuildsHalfResolutionScreenShareLayer) {
	TrackPublishOptions options;
	options.simulcast = true;
	options.video_encoding = {2000000, 15};
	const auto plan = BuildVideoEncodingPlan(1920, 1080, true, options);

	ASSERT_EQ(plan.encodings.size(), 2u);
	EXPECT_EQ(plan.encodings[0].rid, "q");
	EXPECT_EQ(plan.encodings[1].rid, "h");
	EXPECT_DOUBLE_EQ(*plan.encodings[0].scale_resolution_down_by, 2.0);
	EXPECT_EQ(plan.layers[0].width(), 960u);
	EXPECT_EQ(plan.layers[0].height(), 540u);
	EXPECT_EQ(plan.layers[0].bitrate(), 500000u);
}

TEST(VideoEncodingTest, AppliesMatchingCodecQualitiesToSimulcastLayers) {
	TrackPublishOptions options;
	const auto plan = BuildVideoEncodingPlan(1280, 720, false, options);
	auto encodings = plan.encodings;
	SubscribedQualityUpdate update;
	update.codecs = {
	    {"video/VP8",
	     {{VideoQuality::Low, true}, {VideoQuality::Medium, false}, {VideoQuality::High, false}}}};

	EXPECT_TRUE(ApplySubscribedQualities(encodings, update, "vp8"));
	EXPECT_TRUE(encodings[0].active);
	EXPECT_FALSE(encodings[1].active);
	EXPECT_FALSE(encodings[2].active);
	EXPECT_FALSE(ApplySubscribedQualities(encodings, update, "vp8"));
}

TEST(VideoEncodingTest, NeverDisablesSingleEncodingOrMismatchedCodec) {
	TrackPublishOptions options;
	options.simulcast = false;
	auto single = BuildVideoEncodingPlan(1280, 720, false, options).encodings;
	SubscribedQualityUpdate update;
	update.qualities = {{VideoQuality::High, false}};
	EXPECT_FALSE(ApplySubscribedQualities(single, update, "vp8"));
	EXPECT_TRUE(single[0].active);

	options.simulcast = true;
	auto simulcast = BuildVideoEncodingPlan(1280, 720, false, options).encodings;
	update.qualities.clear();
	update.codecs = {{"h264", {{VideoQuality::High, false}}}};
	EXPECT_FALSE(ApplySubscribedQualities(simulcast, update, "vp8"));
	EXPECT_TRUE(simulcast[2].active);
}

} // namespace
} // namespace livekit::core
