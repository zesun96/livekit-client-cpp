#include "../../src/core/participant/remote_participant.h"

#include "livekit_models.pb.h"

#include <gtest/gtest.h>

namespace livekit::core {
namespace {

livekit::TrackInfo MakeTrack(const std::string& sid, const std::string& name,
                             livekit::TrackType kind, livekit::TrackSource source, bool muted) {
	livekit::TrackInfo track;
	track.set_sid(sid);
	track.set_name(name);
	track.set_type(kind);
	track.set_source(source);
	track.set_muted(muted);
	track.set_width(1280);
	track.set_height(720);
	track.set_mime_type(kind == livekit::TrackType::AUDIO ? "audio/opus" : "video/VP8");
	track.set_simulcast(kind == livekit::TrackType::VIDEO);
	return track;
}

TEST(ParticipantStateTest, ReconcilesTrackPublicationsAndMediaState) {
	livekit::ParticipantInfo info;
	info.set_sid("PA_remote");
	info.set_identity("remote");
	info.set_name("Remote User");
	info.set_metadata("version-1");
	(*info.mutable_attributes())["role"] = "speaker";
	*info.add_tracks() = MakeTrack("TR_audio", "microphone", livekit::TrackType::AUDIO,
	                               livekit::TrackSource::MICROPHONE, false);
	*info.add_tracks() = MakeTrack("TR_video", "camera", livekit::TrackType::VIDEO,
	                               livekit::TrackSource::CAMERA, true);

	RemoteParticipant participant(info);
	EXPECT_EQ(participant.GetTrackPublications().size(), 2u);
	auto* microphone = participant.GetTrackPublication(TrackSource::Microphone);
	ASSERT_NE(microphone, nullptr);
	EXPECT_EQ(microphone->Sid(), "TR_audio");
	EXPECT_EQ(microphone->Name(), "microphone");
	EXPECT_EQ(microphone->Kind(), TrackKind::Audio);
	EXPECT_EQ(microphone->MimeType(), "audio/opus");
	EXPECT_FALSE(microphone->IsMuted());
	EXPECT_FALSE(microphone->IsSimulcasted());
	EXPECT_EQ(microphone->Track(), nullptr);
	EXPECT_EQ(participant.GetTrackPublicationByName("microphone"), microphone);
	EXPECT_TRUE(participant.IsMicrophoneEnabled());
	EXPECT_FALSE(participant.IsCameraEnabled());
	EXPECT_FALSE(participant.IsScreenShareEnabled());

	livekit::ParticipantInfo updated = info;
	updated.set_metadata("version-2");
	updated.mutable_attributes()->clear();
	(*updated.mutable_attributes())["role"] = "viewer";
	updated.clear_tracks();
	*updated.add_tracks() = MakeTrack("TR_audio", "microphone", livekit::TrackType::AUDIO,
	                                  livekit::TrackSource::MICROPHONE, true);
	*updated.add_tracks() = MakeTrack("TR_screen", "screen", livekit::TrackType::VIDEO,
	                                  livekit::TrackSource::SCREEN_SHARE, false);
	participant.UpdateFromInfo(updated);

	EXPECT_EQ(participant.Metadata(), "version-2");
	EXPECT_EQ(participant.Attributes().at("role"), "viewer");
	EXPECT_EQ(participant.GetTrackPublications().size(), 2u);
	EXPECT_EQ(participant.GetTrackPublication(TrackSource::Camera), nullptr);
	EXPECT_FALSE(participant.IsMicrophoneEnabled());
	EXPECT_TRUE(participant.IsScreenShareEnabled());
}

TEST(ParticipantStateTest, TracksSpeakerAndConnectionQualityState) {
	livekit::ParticipantInfo info;
	info.set_sid("PA_remote");
	RemoteParticipant participant(info);

	participant.SetSpeakerInfo(0.75f, true);
	participant.SetConnectionQuality(ConnectionQuality::Excellent);

	EXPECT_TRUE(participant.IsSpeaking());
	EXPECT_FLOAT_EQ(participant.AudioLevel(), 0.75f);
	EXPECT_EQ(participant.GetConnectionQuality(), ConnectionQuality::Excellent);
}

TEST(ParticipantStateTest, IgnoresOutOfOrderParticipantSnapshots) {
	livekit::ParticipantInfo current;
	current.set_sid("PA_remote");
	current.set_version(2);
	current.set_metadata("new");
	*current.add_tracks() = MakeTrack("TR_video", "camera", livekit::TrackType::VIDEO,
	                                  livekit::TrackSource::CAMERA, false);
	RemoteParticipant participant(current);

	livekit::ParticipantInfo stale;
	stale.set_sid("PA_remote");
	stale.set_version(1);
	stale.set_metadata("old");
	participant.UpdateFromInfo(stale);

	EXPECT_EQ(participant.Metadata(), "new");
	EXPECT_NE(participant.GetTrackPublication(TrackSource::Camera), nullptr);
}

} // namespace
} // namespace livekit::core
