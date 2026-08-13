#include "livekit/core/livekit_client.h"
#include "livekit/core/rpc.h"
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

TEST(PublicApiTest, RegistersRpcMethodsAndValidatesLocalPayload) {
	auto room = CreateRoomUnique();
	ASSERT_NE(room, nullptr);
	EXPECT_FALSE(
	    room->RegisterRpcMethod("", [](const RpcInvocationData&) { return RpcResult::Success(); }));
	EXPECT_TRUE(room->RegisterRpcMethod("echo", [](const RpcInvocationData& invocation) {
		return RpcResult::Success(invocation.payload);
	}));
	EXPECT_FALSE(room->RegisterRpcMethod(
	    "echo", [](const RpcInvocationData&) { return RpcResult::Success(); }));
	EXPECT_TRUE(room->UnregisterRpcMethod("echo"));
	EXPECT_FALSE(room->UnregisterRpcMethod("echo"));

	PerformRpcParams params;
	params.destination_identity = "destination";
	params.method = "echo";
	params.payload.assign(kMaximumRpcPayloadBytes + 1, 'x');
	const auto result = room->GetLocalParticipant()->PerformRpc(params);
	ASSERT_FALSE(result.Ok());
	ASSERT_TRUE(result.error.has_value());
	EXPECT_EQ(result.error->code, RpcErrorCode::RequestPayloadTooLarge);
	EXPECT_EQ(result.error->message, "Request payload too large");
}

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
