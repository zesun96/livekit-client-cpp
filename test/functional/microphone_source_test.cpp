#include "microphone_audio_source.h"

#include "livekit/core/livekit_client.h"

#include <gtest/gtest.h>

namespace livekit::core {
namespace {

TEST(MicrophoneSourceTest, ValidatesAndRetainsRuntimeVolume) {
	ASSERT_TRUE(Init());
	{
		MicrophoneAudioSource source({});

		EXPECT_FLOAT_EQ(source.Volume(), 1.0F);
		EXPECT_FALSE(source.SetVolume(-0.01F));
		EXPECT_FALSE(source.SetVolume(1.01F));
		ASSERT_TRUE(source.SetVolume(0.25F));
		EXPECT_FLOAT_EQ(source.Volume(), 0.25F);
		auto processing = source.ProcessingOptions();
		EXPECT_TRUE(processing.echo_cancellation);
		EXPECT_TRUE(processing.auto_gain_control);
		EXPECT_TRUE(processing.noise_suppression);
		processing.echo_cancellation = false;
		processing.auto_gain_control = false;
		processing.noise_suppression = false;
		ASSERT_TRUE(source.SetProcessingOptions(processing));
		processing = source.ProcessingOptions();
		EXPECT_FALSE(processing.echo_cancellation);
		EXPECT_FALSE(processing.auto_gain_control);
		EXPECT_FALSE(processing.noise_suppression);
		const auto stats = source.ProcessingStats();
		EXPECT_FALSE(stats.echo_cancellation_enabled);
		EXPECT_EQ(stats.capture_processing_errors, 0u);
		EXPECT_EQ(stats.render_processing_errors, 0u);
	}
	EXPECT_TRUE(Destroy());
}

} // namespace
} // namespace livekit::core
