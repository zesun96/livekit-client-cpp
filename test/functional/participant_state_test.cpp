#include "../../src/core/detail/rtc_engine.h"
#include "../../src/core/participant/remote_participant.h"
#include "../../src/core/room.h"
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

TEST(TrackStateTest, ReportsEnabledStateChanges) {
	Track track("TR_local", "camera", TrackKind::Video);
	EXPECT_TRUE(track.IsEnabled());
	track.SetEnabled(false);
	EXPECT_FALSE(track.IsEnabled());
	track.SetEnabled(true);
	EXPECT_TRUE(track.IsEnabled());
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
