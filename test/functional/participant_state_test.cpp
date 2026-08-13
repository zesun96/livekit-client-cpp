#include "../../src/core/detail/rtc_engine.h"
#include "../../src/core/participant/remote_participant.h"
#include "../../src/core/room.h"
#include "../../src/core/track/remote_track_publication.h"
#include "../../src/core/track/track.h"
#include "../../src/core/track/track_publication.h"

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

TEST(ParticipantStateTest, FindsRemoteParticipantByIdentity) {
	Room room;
	livekit::ParticipantInfo info;
	info.set_sid("PA_remote");
	info.set_identity("remote-identity");
	info.set_name("Remote User");
	room.ParticipantUpdateEvent({info});

	auto* participant = room.GetRemoteParticipantByIdentity("remote-identity");
	ASSERT_NE(participant, nullptr);
	EXPECT_EQ(participant->Sid(), "PA_remote");
	EXPECT_EQ(room.GetParticipantByIdentity("remote-identity"), participant);
	EXPECT_EQ(room.GetRemoteParticipantByIdentity("Remote User"), nullptr);
}

class SubscriptionPermissionEvents final : public RoomEventInterface {
public:
	void OnConnected() override {}
	void OnTrackSubscriptionPermissionChanged(TrackPublicationInterface* publication,
	                                          RemoteParticipantInterface*, bool allowed) override {
		++count;
		track_sid = publication != nullptr ? publication->Sid() : "";
		last_allowed = allowed;
	}

	int count = 0;
	std::string track_sid;
	bool last_allowed = true;
};

TEST(ParticipantStateTest, AppliesSubscriptionPermissionUpdates) {
	Room room;
	SubscriptionPermissionEvents events;
	room.AddEventListener(&events);
	livekit::ParticipantInfo info;
	info.set_sid("PA_publisher");
	info.set_identity("publisher");
	*info.add_tracks() = MakeTrack("TR_video", "camera", livekit::TrackType::VIDEO,
	                               livekit::TrackSource::CAMERA, false);
	room.ParticipantUpdateEvent({info});
	auto* participant = room.GetRemoteParticipantBySid("PA_publisher");
	ASSERT_NE(participant, nullptr);
	auto* publication = participant->GetTrackPublication(TrackSource::Camera);
	ASSERT_NE(publication, nullptr);
	EXPECT_TRUE(publication->IsSubscriptionAllowed());

	livekit::SubscriptionPermissionUpdate denied;
	denied.set_participant_sid("PA_publisher");
	denied.set_track_sid("TR_video");
	denied.set_allowed(false);
	room.SubscriptionPermissionUpdateEvent(denied);
	EXPECT_FALSE(publication->IsSubscriptionAllowed());
	EXPECT_EQ(events.count, 1);
	EXPECT_EQ(events.track_sid, "TR_video");
	EXPECT_FALSE(events.last_allowed);

	room.SubscriptionPermissionUpdateEvent(denied);
	EXPECT_EQ(events.count, 1);
	denied.set_allowed(true);
	room.SubscriptionPermissionUpdateEvent(denied);
	EXPECT_TRUE(publication->IsSubscriptionAllowed());
	EXPECT_EQ(events.count, 2);
	EXPECT_TRUE(events.last_allowed);
	room.RemoveEventListener();
}

class SubscriptionFailureEvents final : public RoomEventInterface {
public:
	void OnConnected() override {}
	void OnTrackSubscriptionFailed(const std::string& sid, RemoteParticipantInterface* participant,
	                               SubscriptionError error) override {
		++count;
		track_sid = sid;
		participant_sid = participant != nullptr ? participant->Sid() : "";
		last_error = error;
	}

	int count = 0;
	std::string track_sid;
	std::string participant_sid;
	SubscriptionError last_error = SubscriptionError::Unknown;
};

TEST(ParticipantStateTest, ReportsAndRetainsSubscriptionFailures) {
	Room room;
	SubscriptionFailureEvents events;
	room.AddEventListener(&events);
	livekit::ParticipantInfo info;
	info.set_sid("PA_publisher");
	*info.add_tracks() = MakeTrack("TR_video", "camera", livekit::TrackType::VIDEO,
	                               livekit::TrackSource::CAMERA, false);
	room.ParticipantUpdateEvent({info});
	auto* participant = room.GetRemoteParticipantBySid("PA_publisher");
	ASSERT_NE(participant, nullptr);
	auto* publication = participant->GetTrackPublication(TrackSource::Camera);
	ASSERT_NE(publication, nullptr);
	EXPECT_FALSE(publication->LastSubscriptionError().has_value());

	livekit::SubscriptionResponse response;
	response.set_track_sid("TR_video");
	response.set_err(livekit::SE_CODEC_UNSUPPORTED);
	room.SubscriptionErrorEvent(response);

	ASSERT_TRUE(publication->LastSubscriptionError().has_value());
	EXPECT_EQ(*publication->LastSubscriptionError(), SubscriptionError::CodecUnsupported);
	EXPECT_EQ(events.count, 1);
	EXPECT_EQ(events.track_sid, "TR_video");
	EXPECT_EQ(events.participant_sid, "PA_publisher");
	EXPECT_EQ(events.last_error, SubscriptionError::CodecUnsupported);

	response.set_track_sid("TR_missing");
	response.set_err(livekit::SE_TRACK_NOTFOUND);
	room.SubscriptionErrorEvent(response);
	EXPECT_EQ(events.count, 1);
	room.RemoveEventListener();
}

TEST(ParticipantStateTest, ControlsAndRetainsRemoteTrackPreferences) {
	livekit::ParticipantInfo info;
	info.set_sid("PA_remote");
	*info.add_tracks() = MakeTrack("TR_video", "camera", livekit::TrackType::VIDEO,
	                               livekit::TrackSource::CAMERA, false);

	int subscription_updates = 0;
	int settings_updates = 0;
	int status_updates = 0;
	bool last_subscribed = true;
	RemoteTrackSettings last_settings;
	TrackSubscriptionStatus last_status = TrackSubscriptionStatus::Desired;
	RemoteParticipant::PublicationHandlers handlers;
	handlers.subscription = [&](const std::string& sid, bool subscribed) {
		EXPECT_EQ(sid, "TR_video");
		++subscription_updates;
		last_subscribed = subscribed;
		return true;
	};
	handlers.settings = [&](const std::string& sid, const RemoteTrackSettings& settings) {
		EXPECT_EQ(sid, "TR_video");
		++settings_updates;
		last_settings = settings;
		return true;
	};
	handlers.status = [&](const std::string& sid, TrackSubscriptionStatus current,
	                      TrackSubscriptionStatus) {
		EXPECT_EQ(sid, "TR_video");
		++status_updates;
		last_status = current;
	};
	RemoteParticipant participant(info, true, std::move(handlers));
	auto* publication = participant.GetTrackPublication(TrackSource::Camera);
	ASSERT_NE(publication, nullptr);
	auto* remote = dynamic_cast<RemoteTrackPublication*>(publication);
	ASSERT_NE(remote, nullptr);
	EXPECT_EQ(publication->SubscriptionStatus(), TrackSubscriptionStatus::Desired);

	EXPECT_TRUE(publication->SetSubscribed(false));
	EXPECT_FALSE(last_subscribed);
	EXPECT_EQ(publication->SubscriptionStatus(), TrackSubscriptionStatus::Unsubscribed);
	EXPECT_EQ(last_status, TrackSubscriptionStatus::Unsubscribed);
	EXPECT_EQ(status_updates, 1);
	RemoteTrackSettings settings;
	settings.video_quality = VideoQuality::Medium;
	EXPECT_FALSE(publication->UpdateRemoteTrackSettings(settings));

	EXPECT_TRUE(publication->SetSubscribed(true));
	EXPECT_EQ(publication->SubscriptionStatus(), TrackSubscriptionStatus::Desired);
	EXPECT_TRUE(publication->SetSubscribed(true));
	EXPECT_EQ(subscription_updates, 3);
	EXPECT_EQ(status_updates, 2);
	settings.video_dimensions = TrackDimensions{640, 360};
	EXPECT_FALSE(publication->UpdateRemoteTrackSettings(settings));
	settings.video_quality.reset();
	settings.video_fps = 24;
	settings.priority = 1;
	EXPECT_TRUE(publication->UpdateRemoteTrackSettings(settings));
	EXPECT_EQ(settings_updates, 1);
	ASSERT_TRUE(last_settings.video_dimensions.has_value());
	EXPECT_EQ(last_settings.video_dimensions->width, 640u);
	EXPECT_EQ(last_settings.video_dimensions->height, 360u);
	EXPECT_EQ(last_settings.video_fps, 24u);
	EXPECT_EQ(last_settings.priority, 1u);

	EXPECT_TRUE(remote->SetTrackAttached(true));
	EXPECT_EQ(publication->SubscriptionStatus(), TrackSubscriptionStatus::Subscribed);
	EXPECT_TRUE(remote->ResendPreferences());
	EXPECT_EQ(subscription_updates, 4);
	EXPECT_EQ(settings_updates, 2);
}

TEST(TrackStateTest, ReportsEnabledStateChanges) {
	Track track("TR_local", "camera", TrackKind::Video);
	EXPECT_TRUE(track.IsEnabled());
	track.SetEnabled(false);
	EXPECT_FALSE(track.IsEnabled());
	track.SetEnabled(true);
	EXPECT_TRUE(track.IsEnabled());
	EXPECT_EQ(track.StreamState(), TrackStreamState::Unknown);
	EXPECT_TRUE(track.SetStreamState(TrackStreamState::Active));
	EXPECT_FALSE(track.SetStreamState(TrackStreamState::Active));
	EXPECT_TRUE(track.SetStreamState(TrackStreamState::Paused));
	EXPECT_EQ(track.StreamState(), TrackStreamState::Paused);
}

TEST(RtcEngineStateTest, RetainsNewestTokenForReconnect) {
	RtcEngine engine;
	EXPECT_TRUE(engine.AccessTokenForReconnect().empty());

	engine.OnTokenRefresh("refreshed-token");
	EXPECT_EQ(engine.AccessTokenForReconnect(), "refreshed-token");

	engine.OnTokenRefresh("");
	EXPECT_EQ(engine.AccessTokenForReconnect(), "refreshed-token");

	engine.Disconnect();
	EXPECT_TRUE(engine.AccessTokenForReconnect().empty());
}

class LocalTrackEvents final : public RoomEventInterface {
public:
	void OnConnected() override {}

	void OnLocalTrackUnpublished(TrackPublicationInterface* publication,
	                             ParticipantInterface* participant) override {
		++unpublished_count;
		track_sid = publication != nullptr ? publication->Sid() : "";
		participant_is_local = participant != nullptr && participant->IsLocalParticipant();
	}

	int unpublished_count = 0;
	std::string track_sid;
	bool participant_is_local = false;
};

class ConnectionEvents final : public RoomEventInterface {
public:
	void OnConnected() override { ++connected_count; }
	void OnReconnecting() override { ++reconnecting_count; }
	void OnReconnected() override { ++reconnected_count; }
	void OnDisconnected() override { ++disconnected_count; }

	int connected_count = 0;
	int reconnecting_count = 0;
	int reconnected_count = 0;
	int disconnected_count = 0;
};

class DetailedConnectionEvents final : public RoomEventInterface {
public:
	void OnConnected() override {}
	void OnDisconnected(DisconnectReason reason) override {
		++disconnected_count;
		disconnect_reason = reason;
	}

	int disconnected_count = 0;
	DisconnectReason disconnect_reason = DisconnectReason::Unknown;
};

TEST(RoomConnectionStateTest, TransitionsThroughSuccessfulReconnect) {
	Room room;
	ConnectionEvents events;
	room.AddEventListener(&events);
	room.ConnectedEvent({});
	ASSERT_EQ(room.State(), RoomInterface::RoomState::Connected);

	room.ReconnectingEvent(false);
	EXPECT_EQ(room.State(), RoomInterface::RoomState::Reconnecting);
	EXPECT_FALSE(room.IsConnected());
	EXPECT_EQ(events.reconnecting_count, 1);

	room.ResumedEvent();
	EXPECT_EQ(room.State(), RoomInterface::RoomState::Connected);
	EXPECT_TRUE(room.IsConnected());
	EXPECT_EQ(events.reconnected_count, 1);

	EXPECT_TRUE(room.Disconnect());
	EXPECT_EQ(room.LastDisconnectReason(), DisconnectReason::ClientInitiated);
	room.RemoveEventListener();
}

TEST(RoomConnectionStateTest, ReportsUnexpectedSignalCloseOnlyOnce) {
	Room room;
	ConnectionEvents events;
	room.AddEventListener(&events);
	room.ConnectedEvent({});
	ASSERT_EQ(room.State(), RoomInterface::RoomState::Connected);

	room.SignalDisconnectedEvent(livekit::DisconnectReason::SIGNAL_CLOSE);
	EXPECT_EQ(room.State(), RoomInterface::RoomState::Failed);
	EXPECT_FALSE(room.IsConnected());
	EXPECT_EQ(events.disconnected_count, 1);
	EXPECT_EQ(room.LastDisconnectReason(), DisconnectReason::SignalClose);

	room.SignalDisconnectedEvent(livekit::DisconnectReason::SIGNAL_CLOSE);
	EXPECT_EQ(events.disconnected_count, 1);
	EXPECT_TRUE(room.Disconnect());
	EXPECT_EQ(room.State(), RoomInterface::RoomState::Disconnected);
	EXPECT_EQ(events.disconnected_count, 1);
	room.RemoveEventListener();
}

TEST(RoomConnectionStateTest, PreservesDetailedServerDisconnectReason) {
	Room room;
	DetailedConnectionEvents events;
	room.AddEventListener(&events);
	room.ConnectedEvent({});

	room.SignalDisconnectedEvent(livekit::DisconnectReason::PARTICIPANT_REMOVED);
	EXPECT_EQ(room.State(), RoomInterface::RoomState::Failed);
	EXPECT_EQ(room.LastDisconnectReason(), DisconnectReason::ParticipantRemoved);
	EXPECT_EQ(events.disconnected_count, 1);
	EXPECT_EQ(events.disconnect_reason, DisconnectReason::ParticipantRemoved);

	EXPECT_TRUE(room.Disconnect());
	EXPECT_EQ(room.LastDisconnectReason(), DisconnectReason::ParticipantRemoved);
	room.RemoveEventListener();
}

class DataStreamEvents final : public RoomEventInterface {
public:
	void OnConnected() override {}
	void OnTextReceived(const TextReceivedEvent& event) override { texts.push_back(event); }
	void OnByteReceived(const ByteReceivedEvent& event) override { bytes.push_back(event); }
	void OnFileReceived(const FileReceivedEvent& event) override { files.push_back(event); }

	std::vector<TextReceivedEvent> texts;
	std::vector<ByteReceivedEvent> bytes;
	std::vector<FileReceivedEvent> files;
};

livekit::DataPacket StreamHeader(const std::string& id, uint64_t size) {
	livekit::DataPacket packet;
	packet.set_participant_identity("sender");
	auto* header = packet.mutable_stream_header();
	header->set_stream_id(id);
	header->set_topic("stream-topic");
	header->set_timestamp(1234);
	header->set_total_length(size);
	(*header->mutable_attributes())["kind"] = "test";
	return packet;
}

livekit::DataPacket StreamChunk(const std::string& id, uint64_t index, const std::string& content) {
	livekit::DataPacket packet;
	auto* chunk = packet.mutable_stream_chunk();
	chunk->set_stream_id(id);
	chunk->set_chunk_index(index);
	chunk->set_content(content);
	return packet;
}

livekit::DataPacket StreamTrailer(const std::string& id) {
	livekit::DataPacket packet;
	packet.mutable_stream_trailer()->set_stream_id(id);
	return packet;
}

TEST(DataStreamStateTest, ReassemblesTextAndByteStreams) {
	Room room;
	DataStreamEvents events;
	room.AddEventListener(&events);

	auto text_header = StreamHeader("text-1", 11);
	auto* text = text_header.mutable_stream_header()->mutable_text_header();
	text->set_reply_to_stream_id("parent");
	text->add_attached_stream_ids("file-1");
	room.DataPacketEvent(text_header);
	room.DataPacketEvent(StreamChunk("text-1", 0, "hello "));
	room.DataPacketEvent(StreamChunk("text-1", 1, "world"));
	room.DataPacketEvent(StreamTrailer("text-1"));
	ASSERT_EQ(events.texts.size(), 1u);
	EXPECT_EQ(events.texts[0].text, "hello world");
	EXPECT_EQ(events.texts[0].reply_to_stream_id, "parent");
	EXPECT_EQ(events.texts[0].attached_stream_ids, std::vector<std::string>{"file-1"});
	EXPECT_EQ(events.texts[0].attributes.at("kind"), "test");
	EXPECT_EQ(events.texts[0].participant_identity, "sender");
	EXPECT_EQ(events.texts[0].timestamp, 1234);

	auto bytes_header = StreamHeader("bytes-1", 4);
	bytes_header.mutable_stream_header()->set_mime_type("application/test");
	bytes_header.mutable_stream_header()->mutable_byte_header();
	room.DataPacketEvent(bytes_header);
	room.DataPacketEvent(StreamChunk("bytes-1", 0, "data"));
	room.DataPacketEvent(StreamTrailer("bytes-1"));
	ASSERT_EQ(events.bytes.size(), 1u);
	EXPECT_EQ(events.bytes[0].data, std::vector<uint8_t>({'d', 'a', 't', 'a'}));
	EXPECT_TRUE(events.files.empty());

	auto file_header = StreamHeader("file-1", 4);
	file_header.mutable_stream_header()->mutable_byte_header()->set_name("test.bin");
	room.DataPacketEvent(file_header);
	room.DataPacketEvent(StreamChunk("file-1", 0, "file"));
	auto file_trailer = StreamTrailer("file-1");
	(*file_trailer.mutable_stream_trailer()->mutable_attributes())["complete"] = "true";
	room.DataPacketEvent(file_trailer);
	ASSERT_EQ(events.bytes.size(), 2u);
	ASSERT_EQ(events.files.size(), 1u);
	EXPECT_EQ(events.files[0].name, "test.bin");
	EXPECT_EQ(events.files[0].attributes.at("complete"), "true");
	EXPECT_EQ(events.files[0].timestamp, 1234);
	room.RemoveEventListener();
}

TEST(DataStreamStateTest, HandlesInlineStreamsAndRejectsInvalidChunks) {
	Room room;
	DataStreamEvents events;
	room.AddEventListener(&events);

	auto inline_text = StreamHeader("inline", 5);
	inline_text.mutable_stream_header()->mutable_text_header();
	inline_text.mutable_stream_header()->set_inline_content("hello");
	room.DataPacketEvent(inline_text);
	ASSERT_EQ(events.texts.size(), 1u);
	EXPECT_EQ(events.texts[0].text, "hello");

	auto invalid = StreamHeader("invalid", 3);
	invalid.mutable_stream_header()->mutable_byte_header()->set_name("bad.bin");
	room.DataPacketEvent(invalid);
	room.DataPacketEvent(StreamChunk("invalid", 1, "bad"));
	room.DataPacketEvent(StreamTrailer("invalid"));
	EXPECT_TRUE(events.bytes.empty());
	EXPECT_TRUE(events.files.empty());
	room.RemoveEventListener();
}

TEST(LocalTrackStateTest, HandlesServerInitiatedUnpublishOnce) {
	Room room;
	LocalTrackEvents events;
	room.AddEventListener(&events);

	livekit::TrackInfo info = MakeTrack("TR_local", "camera", livekit::TrackType::VIDEO,
	                                    livekit::TrackSource::CAMERA, false);
	auto publication = std::make_shared<TrackPublication>(info, nullptr);
	auto* participant = dynamic_cast<LocalParticipant*>(room.GetLocalParticipant());
	ASSERT_NE(participant, nullptr);
	participant->AddTrackPublication(publication);

	room.LocalTrackUnpublishedEvent("TR_local");
	EXPECT_EQ(events.unpublished_count, 1);
	EXPECT_EQ(events.track_sid, "TR_local");
	EXPECT_TRUE(events.participant_is_local);
	EXPECT_EQ(participant->GetTrackPublicationByName("camera"), nullptr);

	room.LocalTrackUnpublishedEvent("TR_local");
	EXPECT_EQ(events.unpublished_count, 1);
	room.RemoveEventListener();
}

} // namespace
} // namespace livekit::core
