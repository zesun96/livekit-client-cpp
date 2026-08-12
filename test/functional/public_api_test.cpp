#include "livekit/core/livekit_client.h"
#include "livekit/core/track/audio_source_interface.h"
#include "livekit/core/track/video_source_interface.h"

#include <gtest/gtest.h>

#include <vector>

namespace livekit::core {
namespace {

TEST(ClientLifecycleTest, InitAndDestroyAreReferenceCounted) {
	ASSERT_TRUE(Init());
	ASSERT_TRUE(Init());
	EXPECT_TRUE(Destroy());
	EXPECT_TRUE(Destroy());
	EXPECT_TRUE(Destroy());
}

TEST(PublicApiTest, CreatesOwnedDisconnectedRoom) {
	auto room = CreateRoomUnique();
	ASSERT_NE(room, nullptr);
	EXPECT_EQ(room->State(), RoomInterface::RoomState::Disconnected);
	EXPECT_FALSE(room->IsConnected());
	EXPECT_NE(room->GetLocalParticipant(), nullptr);
	EXPECT_TRUE(room->Sid().empty());
	EXPECT_TRUE(room->Name().empty());
	EXPECT_TRUE(room->Metadata().empty());
	EXPECT_FALSE(room->IsRecording());
	EXPECT_EQ(room->GetRemoteParticipantByIdentity("missing"), nullptr);
	EXPECT_FALSE(room->SetLocalTrackMuted("missing", true));
	EXPECT_FALSE(room->SetRemoteTrackSubscribed("missing", "missing", true));
}

TEST(PublicApiTest, ExposesSemanticVersion) { EXPECT_EQ(Version(), "0.0.1"); }

TEST(MediaSourceTest, ValidatesI420VideoFrames) {
	ASSERT_TRUE(Init());
	auto source = CreateVideoSourceUnique();
	ASSERT_NE(source, nullptr);

	VideoFrame valid;
	valid.width = 4;
	valid.height = 2;
	valid.data.resize(12, 128);
	EXPECT_TRUE(source->CaptureFrame(valid));

	auto invalid_size = valid;
	invalid_size.data.pop_back();
	EXPECT_FALSE(source->CaptureFrame(invalid_size));
	auto invalid_dimensions = valid;
	invalid_dimensions.width = 3;
	EXPECT_FALSE(source->CaptureFrame(invalid_dimensions));
	source.reset();
	EXPECT_TRUE(Destroy());
}

TEST(MediaSourceTest, AcceptsConsecutiveAudioFrames) {
	ASSERT_TRUE(Init());
	auto source = CreateAudioSourceUnique({}, 48000, 1, 200);
	ASSERT_NE(source, nullptr);
	std::vector<int16_t> samples(480, 100);
	EXPECT_FALSE(source->CaptureFrame(nullptr, 48000, 1, 480));
	EXPECT_TRUE(source->CaptureFrame(samples.data(), 48000, 1, 480));
	EXPECT_TRUE(source->CaptureFrame(samples.data(), 48000, 1, 480));
	source.reset();
	EXPECT_TRUE(Destroy());
}

} // namespace
} // namespace livekit::core
