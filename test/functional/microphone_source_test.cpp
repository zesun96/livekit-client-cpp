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
	}
	EXPECT_TRUE(Destroy());
}

} // namespace
} // namespace livekit::core
