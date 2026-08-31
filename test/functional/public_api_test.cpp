#include "livekit/core/livekit_client.h"
#include "livekit/core/rpc.h"
#include "livekit/core/track/audio_source_interface.h"
#include "livekit/core/track/video_source_interface.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <future>
#include <stdexcept>
#include <utility>
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
	EXPECT_FALSE(room->SetAudioOutputDevice("missing"));
	EXPECT_TRUE(room->AudioOutputDevice().empty());
	EXPECT_FALSE(room->SetSpeakerVolume(0.5F));
	EXPECT_FLOAT_EQ(room->SpeakerVolume(), 1.0F);
	EXPECT_FALSE(room->SetSpeakerMuted(true));
	EXPECT_FALSE(room->SpeakerMuted());
	const auto playback_stats = room->GetAudioPlaybackStats();
	EXPECT_EQ(playback_stats.queued_frames, 0u);
	EXPECT_EQ(playback_stats.played_frames, 0u);
	DataTrackSchema schema{{"test.schema", {DataTrackSchemaEncodingKind::JsonSchema, {}}},
	                       {'{', '}'}};
	EXPECT_EQ(room->StoreDataTrackSchema(schema).code, DataTrackErrorCode::Disconnected);
	EXPECT_EQ(room->GetDataTrackSchema("publisher", schema.id).error.code,
	          DataTrackErrorCode::Disconnected);
}

TEST(PublicApiTest, ProvidesConfigurableReconnectBounds) {
	auto options = default_room_connect_options();
	EXPECT_EQ(options.join_retries, 3u);
	EXPECT_EQ(options.reconnect_timeout, std::chrono::seconds(15));
	ASSERT_NE(options.reconnect_policy, nullptr);

	ReconnectContext context;
	context.retry_count = 1;
	context.elapsed = std::chrono::seconds(2);
	context.reason = ReconnectReason::SignalDisconnected;
	context.server_url = "ws://localhost:7880";
	EXPECT_EQ(options.reconnect_policy->NextRetryDelay(context), std::chrono::milliseconds(300));
}

TEST(PublicApiTest, ExposesSemanticVersion) {
	const auto version = Version();
	EXPECT_EQ(std::count(version.begin(), version.end(), '.'), 2);
	EXPECT_TRUE(std::all_of(version.begin(), version.end(), [](unsigned char value) {
		return std::isdigit(value) != 0 || value == '.';
	}));
}

TEST(PublicApiTest, EnumeratesMediaDevicesWithoutChangingClientState) {
	const auto devices = EnumerateMediaDevices();
	for (const auto& device : devices) {
		EXPECT_FALSE(device.id.empty());
		EXPECT_FALSE(device.label.empty());
		EXPECT_TRUE(device.kind == MediaDeviceKind::AudioInput ||
		            device.kind == MediaDeviceKind::AudioOutput ||
		            device.kind == MediaDeviceKind::VideoInput);
		if (device.is_default) {
			EXPECT_NE(device.kind, MediaDeviceKind::VideoInput);
		}
	}
}

TEST(PublicApiTest, RejectsUnknownCameraDeviceWithoutAStartedSource) {
	CameraCaptureOptions options;
	options.device_id = "livekit-device-that-does-not-exist";
	EXPECT_EQ(CreateCameraVideoSourceUnique(std::move(options)), nullptr);
}

TEST(PublicApiTest, RejectsUnknownMicrophoneDeviceWithoutAStartedSource) {
	MicrophoneCaptureOptions options;
	EXPECT_TRUE(options.processing.echo_cancellation);
	EXPECT_TRUE(options.processing.auto_gain_control);
	EXPECT_TRUE(options.processing.noise_suppression);
	options.device_id = "livekit-device-that-does-not-exist";
	EXPECT_EQ(CreateMicrophoneAudioSourceUnique(std::move(options)), nullptr);
}

TEST(PublicApiTest, RejectsUnknownSystemAudioOutputWithoutChangingExternalPcmSource) {
	SystemAudioCaptureOptions options;
	options.device_id = "livekit-device-that-does-not-exist";
	EXPECT_EQ(CreateSystemAudioSourceUnique(std::move(options)), nullptr);
	auto external = CreateAudioSourceUnique({}, 48000, 2, 200);
	EXPECT_NE(external, nullptr);
}

TEST(PublicApiTest, EnumeratesAndValidatesScreenCaptureSources) {
	for (const auto& source : EnumerateScreenCaptureSources()) {
		EXPECT_FALSE(source.id.empty());
		EXPECT_GT(source.width, 0U);
		EXPECT_GT(source.height, 0U);
	}
	EXPECT_EQ(CreateScreenVideoSource({}), nullptr);
	EXPECT_EQ(CreateScreenVideoSource({"monitor:missing", 15}), nullptr);
}

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

TEST(PublicApiTest, ValidatesAndRetainsTrackSubscriptionPermissionsBeforeConnect) {
	auto room = CreateRoomUnique();
	auto* local = room->GetLocalParticipant();
	ASSERT_NE(local, nullptr);
	EXPECT_TRUE(local->SetTrackSubscriptionPermissions(false));
	ParticipantTrackPermission invalid;
	EXPECT_FALSE(local->SetTrackSubscriptionPermissions(false, {invalid}));
	ParticipantTrackPermission valid;
	valid.participant_identity = "viewer";
	valid.allowed_track_sids = {"TR_audio"};
	EXPECT_TRUE(local->SetTrackSubscriptionPermissions(false, {valid}));
	valid.allowed_track_sids.push_back("");
	EXPECT_FALSE(local->SetTrackSubscriptionPermissions(false, {valid}));
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
	VideoFrame odd;
	odd.width = 3;
	odd.height = 3;
	odd.data.resize(17, 128);
	EXPECT_TRUE(source->CaptureFrame(odd));
	VideoFrame nv12 = valid;
	nv12.format = VideoBufferType::NV12;
	EXPECT_TRUE(source->CaptureFrame(nv12));
	VideoFrame rgba;
	rgba.width = 4;
	rgba.height = 2;
	rgba.format = VideoBufferType::RGBA;
	rgba.data.resize(32, 255);
	EXPECT_TRUE(source->CaptureFrame(rgba));
	auto rotated = valid;
	rotated.rotation = VideoRotation::Rotation90;
	EXPECT_TRUE(source->CaptureFrame(rotated));
	auto invalid_rotation = valid;
	invalid_rotation.rotation = static_cast<VideoRotation>(45);
	EXPECT_FALSE(source->CaptureFrame(invalid_rotation));
	auto with_metadata = valid;
	with_metadata.timestamp_us = 1'000'000;
	with_metadata.metadata = VideoFrameMetadata{};
	with_metadata.metadata->user_timestamp_us = 1'744'249'600'123'456ULL;
	with_metadata.metadata->frame_id = 42;
	with_metadata.metadata->user_data = std::vector<std::uint8_t>{1, 2, 3};
	EXPECT_TRUE(source->CaptureFrame(with_metadata));
	with_metadata.metadata->user_data =
	    std::vector<std::uint8_t>(kMaxVideoFrameMetadataUserDataSize + 1, 0x7f);
	EXPECT_FALSE(source->CaptureFrame(with_metadata));
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

TEST(MediaSourceTest, EnforcesAudioSourceConfigurationAndQueueBounds) {
	ASSERT_TRUE(Init());
	EXPECT_THROW(CreateAudioSourceUnique({}, 0, 1, 200), std::invalid_argument);
	EXPECT_THROW(CreateAudioSourceUnique({}, 48000, 0, 200), std::invalid_argument);
	EXPECT_THROW(CreateAudioSourceUnique({}, 48000, 1, 15), std::invalid_argument);

	std::vector<int16_t> ten_ms(480, 100);
	std::vector<int16_t> twenty_ms(960, 100);
	std::vector<int16_t> thirty_ms(1440, 100);
	std::vector<int16_t> stereo_ten_ms(960, 100);

	auto bounded = CreateAudioSourceUnique({}, 48000, 1, 20);
	ASSERT_NE(bounded, nullptr);
	EXPECT_TRUE(bounded->CaptureFrame(twenty_ms.data(), 48000, 1, 960));
	EXPECT_FALSE(bounded->CaptureFrame(ten_ms.data(), 48000, 1, 479));

	auto oversized = CreateAudioSourceUnique({}, 48000, 1, 20);
	ASSERT_NE(oversized, nullptr);
	EXPECT_FALSE(oversized->CaptureFrame(thirty_ms.data(), 48000, 1, 1440));
	EXPECT_FALSE(oversized->CaptureFrame(ten_ms.data(), 44100, 1, 441));
	EXPECT_FALSE(oversized->CaptureFrame(stereo_ten_ms.data(), 48000, 2, 480));

	auto direct = CreateAudioSourceUnique({}, 48000, 1, 0);
	ASSERT_NE(direct, nullptr);
	EXPECT_TRUE(direct->CaptureFrame(ten_ms.data(), 48000, 1, 480));
	EXPECT_FALSE(direct->CaptureFrame(ten_ms.data(), 48000, 1, 479));
	EXPECT_EQ(direct->QueuedDuration(), std::chrono::milliseconds::zero());
	EXPECT_TRUE(direct->ClearQueue());
	EXPECT_TRUE(direct->WaitForPlayout(std::chrono::milliseconds::zero()));

	std::vector<int16_t> two_hundred_ms(9600, 100);
	auto controlled = CreateAudioSourceUnique({}, 48000, 1, 200);
	ASSERT_NE(controlled, nullptr);
	ASSERT_TRUE(controlled->CaptureFrame(two_hundred_ms.data(), 48000, 1, 9600));
	EXPECT_GT(controlled->QueuedDuration(), std::chrono::milliseconds::zero());
	EXPECT_LE(controlled->QueuedDuration(), std::chrono::milliseconds(200));
	EXPECT_FALSE(controlled->WaitForPlayout(std::chrono::milliseconds::zero()));
	auto waiter = std::async(std::launch::async, [&controlled] {
		return controlled->WaitForPlayout(std::chrono::seconds(2));
	});
	EXPECT_EQ(waiter.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
	EXPECT_TRUE(controlled->ClearQueue());
	EXPECT_EQ(waiter.wait_for(std::chrono::milliseconds(500)), std::future_status::ready);
	EXPECT_TRUE(waiter.get());
	EXPECT_EQ(controlled->QueuedDuration(), std::chrono::milliseconds::zero());
	EXPECT_TRUE(controlled->WaitForPlayout(std::chrono::milliseconds::zero()));
	ASSERT_TRUE(controlled->CaptureFrame(twenty_ms.data(), 48000, 1, 960));
	EXPECT_TRUE(controlled->WaitForPlayout(std::chrono::milliseconds(500)));

	controlled.reset();
	direct.reset();
	oversized.reset();
	bounded.reset();
	EXPECT_TRUE(Destroy());
}

} // namespace
} // namespace livekit::core
