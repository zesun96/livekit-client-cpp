#include "../../src/core/detail/rtc_engine.h"
#include "../../src/core/participant/remote_participant.h"
#include "../../src/core/room.h"
#include "../../src/core/track/remote_track_publication.h"
#include "../../src/core/track/track.h"
#include "../../src/core/track/track_publication.h"
#include "data_stream_compression.h"

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

TEST(ParticipantStateTest, ReplacesReconnectedParticipantWithSameIdentity) {
	Room room;
	livekit::ParticipantInfo original;
	original.set_sid("PA_before_reconnect");
	original.set_identity("stable-identity");
	original.set_version(3);
	room.ParticipantUpdateEvent({original});

	livekit::ParticipantInfo reconnected = original;
	reconnected.set_sid("PA_after_reconnect");
	reconnected.set_version(1);
	room.ParticipantUpdateEvent({reconnected});

	EXPECT_EQ(room.GetRemoteParticipantBySid("PA_before_reconnect"), nullptr);
	auto* current = room.GetRemoteParticipantByIdentity("stable-identity");
	ASSERT_NE(current, nullptr);
	EXPECT_EQ(current->Sid(), "PA_after_reconnect");
	ASSERT_EQ(room.GetRemoteParticipants().size(), 1u);
}

TEST(ParticipantStateTest, CreatesOwnedRemoteParticipantSnapshots) {
	Room room;
	livekit::ParticipantInfo info;
	info.set_sid("PA_snapshot");
	info.set_identity("snapshot-identity");
	info.set_name("Snapshot User");
	info.set_metadata("snapshot-metadata");
	(*info.mutable_attributes())["role"] = "speaker";
	info.mutable_permission()->set_can_subscribe(true);
	info.mutable_permission()->set_can_publish(true);
	info.mutable_permission()->add_can_publish_sources(livekit::TrackSource::CAMERA);
	*info.add_tracks() = MakeTrack("TR_snapshot", "camera", livekit::TrackType::VIDEO,
	                               livekit::TrackSource::CAMERA, false);
	room.ParticipantUpdateEvent({info});

	livekit::SpeakerInfo speaker;
	speaker.set_sid("PA_snapshot");
	speaker.set_level(0.625f);
	speaker.set_active(true);
	room.SpeakersChangedEvent({speaker});
	livekit::ConnectionQualityInfo quality;
	quality.set_participant_sid("PA_snapshot");
	quality.set_quality(livekit::ConnectionQuality::EXCELLENT);
	room.ConnectionQualityEvent({quality});

	auto snapshots = room.GetRemoteParticipantSnapshots();
	ASSERT_EQ(snapshots.size(), 1u);
	const auto& participant = snapshots.front();
	EXPECT_EQ(participant.sid, "PA_snapshot");
	EXPECT_EQ(participant.identity, "snapshot-identity");
	EXPECT_EQ(participant.name, "Snapshot User");
	EXPECT_EQ(participant.metadata, "snapshot-metadata");
	EXPECT_EQ(participant.attributes.at("role"), "speaker");
	EXPECT_FLOAT_EQ(participant.audio_level, 0.625f);
	EXPECT_TRUE(participant.speaking);
	EXPECT_EQ(participant.connection_quality, ConnectionQuality::Excellent);
	EXPECT_TRUE(participant.permissions.can_subscribe);
	EXPECT_TRUE(participant.permissions.can_publish);
	ASSERT_EQ(participant.permissions.can_publish_sources.size(), 1u);
	EXPECT_EQ(participant.permissions.can_publish_sources.front(), TrackSource::Camera);
	ASSERT_EQ(participant.publications.size(), 1u);
	const auto& publication = participant.publications.front();
	EXPECT_EQ(publication.sid, "TR_snapshot");
	EXPECT_EQ(publication.name, "camera");
	EXPECT_EQ(publication.mime_type, "video/VP8");
	EXPECT_EQ(publication.kind, TrackKind::Video);
	EXPECT_EQ(publication.source, TrackSource::Camera);
	EXPECT_EQ(publication.dimensions.width, 1280u);
	EXPECT_EQ(publication.subscription_status, TrackSubscriptionStatus::Desired);
	EXPECT_FALSE(publication.subscribed_track.has_value());

	livekit::ParticipantInfo disconnected = info;
	disconnected.set_state(livekit::ParticipantInfo::DISCONNECTED);
	room.ParticipantUpdateEvent({disconnected});
	EXPECT_TRUE(room.GetRemoteParticipantSnapshots().empty());
	EXPECT_EQ(snapshots.front().identity, "snapshot-identity");
	EXPECT_EQ(snapshots.front().publications.front().sid, "TR_snapshot");
}

class DataTrackEvents final : public RoomEventInterface {
public:
	void OnConnected() override {}
	void OnDataTrackPublished(RemoteDataTrackInterface*, RemoteParticipantInterface*) override {
		++published_count;
	}
	void OnDataTrackUnpublished(DataTrackInterface*, RemoteParticipantInterface*) override {
		++unpublished_count;
	}

	int published_count = 0;
	int unpublished_count = 0;
};

TEST(ParticipantStateTest, PreservesDataTrackWhenSidIsReassigned) {
	Room room;
	DataTrackEvents events;
	room.AddEventListener(&events);

	livekit::ParticipantInfo info;
	info.set_sid("PA_data_track");
	info.set_identity("data-track-publisher");
	info.set_version(1);
	auto* data_track = info.add_data_tracks();
	data_track->set_sid("DT_before_reconnect");
	data_track->set_name("telemetry");
	data_track->set_pub_handle(7);
	room.ParticipantUpdateEvent({info});

	auto* participant = room.GetRemoteParticipantByIdentity("data-track-publisher");
	ASSERT_NE(participant, nullptr);
	auto* original_track = participant->GetDataTrackBySid("DT_before_reconnect");
	ASSERT_NE(original_track, nullptr);
	EXPECT_TRUE(original_track->IsPublished());
	auto owned_track = room.GetRemoteDataTrack("data-track-publisher", "DT_before_reconnect");
	ASSERT_NE(owned_track, nullptr);
	EXPECT_EQ(owned_track.get(), original_track);
	auto snapshots = room.GetRemoteDataTrackSnapshots();
	ASSERT_EQ(snapshots.size(), 1u);
	EXPECT_EQ(snapshots.front().publisher_identity, "data-track-publisher");
	EXPECT_EQ(snapshots.front().info.sid, "DT_before_reconnect");
	EXPECT_EQ(snapshots.front().info.name, "telemetry");
	EXPECT_TRUE(snapshots.front().published);
	EXPECT_EQ(events.published_count, 1);
	EXPECT_EQ(events.unpublished_count, 0);

	livekit::ParticipantInfo reconnected = info;
	reconnected.set_version(2);
	reconnected.mutable_data_tracks(0)->set_sid("DT_after_reconnect");
	room.ParticipantUpdateEvent({reconnected});

	participant = room.GetRemoteParticipantByIdentity("data-track-publisher");
	ASSERT_NE(participant, nullptr);
	EXPECT_EQ(participant->GetDataTrackBySid("DT_before_reconnect"), nullptr);
	auto* reassigned_track = participant->GetDataTrackBySid("DT_after_reconnect");
	EXPECT_EQ(reassigned_track, original_track);
	ASSERT_NE(reassigned_track, nullptr);
	EXPECT_TRUE(reassigned_track->IsPublished());
	EXPECT_EQ(room.GetRemoteDataTrack("data-track-publisher", "DT_after_reconnect").get(),
	          original_track);
	auto reconnected_snapshots = room.GetRemoteDataTrackSnapshots();
	ASSERT_EQ(reconnected_snapshots.size(), 1u);
	EXPECT_EQ(reconnected_snapshots.front().info.sid, "DT_after_reconnect");
	EXPECT_EQ(snapshots.front().info.sid, "DT_before_reconnect");
	EXPECT_EQ(events.published_count, 1);
	EXPECT_EQ(events.unpublished_count, 0);
	room.RemoveEventListener();
}

class RoomStateEvents final : public RoomEventInterface {
public:
	void OnConnected() override {}
	void OnRoomMetadataChanged(const std::string& metadata) override {
		++metadata_change_count;
		last_metadata = metadata;
	}
	void OnRecordingStatusChanged(bool recording) override {
		recording_states.push_back(recording);
	}

	int metadata_change_count = 0;
	std::string last_metadata;
	std::vector<bool> recording_states;
};

TEST(RoomStateTest, ReportsRecordingChangesOnlyWhenStateTransitions) {
	Room room;
	RoomStateEvents events;
	room.AddEventListener(&events);
	livekit::Room update;
	update.set_metadata("room metadata");
	room.RoomUpdateEvent(update);
	EXPECT_EQ(events.metadata_change_count, 1);
	EXPECT_EQ(events.last_metadata, "room metadata");
	EXPECT_TRUE(events.recording_states.empty());
	EXPECT_FALSE(room.IsRecording());

	update.set_active_recording(true);
	room.RoomUpdateEvent(update);
	ASSERT_EQ(events.recording_states.size(), 1u);
	EXPECT_TRUE(events.recording_states[0]);
	EXPECT_TRUE(room.IsRecording());

	room.RoomUpdateEvent(update);
	EXPECT_EQ(events.recording_states.size(), 1u);
	update.set_active_recording(false);
	room.RoomUpdateEvent(update);
	ASSERT_EQ(events.recording_states.size(), 2u);
	EXPECT_FALSE(events.recording_states[1]);
	EXPECT_FALSE(room.IsRecording());
	room.RemoveEventListener();
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

class ParticipantPermissionEvents final : public RoomEventInterface {
public:
	void OnConnected() override {}
	void OnParticipantPermissionsChanged(const ParticipantPermissions& previous_permissions,
	                                     ParticipantInterface* participant) override {
		++count;
		previous = previous_permissions;
		current = participant->Permissions();
		participant_identity = participant->Identity();
	}

	int count = 0;
	ParticipantPermissions previous;
	ParticipantPermissions current;
	std::string participant_identity;
};

TEST(ParticipantStateTest, ReportsCompleteParticipantPermissionChanges) {
	Room room;
	ParticipantPermissionEvents events;
	room.AddEventListener(&events);
	livekit::ParticipantInfo info;
	info.set_sid("PA_permissions");
	info.set_identity("permissions-user");
	info.set_version(1);
	info.mutable_permission()->set_can_subscribe(true);
	info.mutable_permission()->set_can_publish(false);
	info.mutable_permission()->set_can_publish_data(true);
	info.mutable_permission()->add_can_publish_sources(livekit::TrackSource::MICROPHONE);
	room.ParticipantUpdateEvent({info});
	EXPECT_EQ(events.count, 0);
	auto* participant = room.GetRemoteParticipantBySid("PA_permissions");
	ASSERT_NE(participant, nullptr);
	EXPECT_TRUE(participant->Permissions().can_subscribe);
	EXPECT_EQ(participant->Permissions().can_publish_sources,
	          std::vector<TrackSource>{TrackSource::Microphone});

	info.set_version(2);
	auto* permission = info.mutable_permission();
	permission->set_can_subscribe(false);
	permission->set_can_publish(true);
	permission->set_can_publish_data(false);
	permission->add_can_publish_sources(livekit::TrackSource::CAMERA);
	permission->set_hidden(true);
	permission->set_recorder(true);
	permission->set_can_update_metadata(true);
	permission->set_agent(true);
	permission->set_can_subscribe_metrics(true);
	permission->set_can_manage_agent_session(true);
	room.ParticipantUpdateEvent({info});

	EXPECT_EQ(events.count, 1);
	EXPECT_EQ(events.participant_identity, "permissions-user");
	EXPECT_TRUE(events.previous.can_subscribe);
	EXPECT_FALSE(events.previous.can_publish);
	EXPECT_FALSE(events.current.can_subscribe);
	EXPECT_TRUE(events.current.can_publish);
	EXPECT_FALSE(events.current.can_publish_data);
	EXPECT_EQ(events.current.can_publish_sources,
	          (std::vector<TrackSource>{TrackSource::Microphone, TrackSource::Camera}));
	EXPECT_TRUE(events.current.hidden);
	EXPECT_TRUE(events.current.recorder);
	EXPECT_TRUE(events.current.can_update_metadata);
	EXPECT_TRUE(events.current.agent);
	EXPECT_TRUE(events.current.can_subscribe_metrics);
	EXPECT_TRUE(events.current.can_manage_agent_session);

	room.ParticipantUpdateEvent({info});
	EXPECT_EQ(events.count, 1);
	room.RemoveEventListener();
}

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
	void OnLocalTrackSubscribed(TrackPublicationInterface* publication,
	                            ParticipantInterface* participant) override {
		++subscribed_count;
		subscribed_track_sid = publication != nullptr ? publication->Sid() : "";
		subscriber_event_is_local = participant != nullptr && participant->IsLocalParticipant();
	}
	void OnSubscribedQualityUpdate(TrackPublicationInterface* publication,
	                               ParticipantInterface* participant,
	                               const SubscribedQualityUpdate& update) override {
		++quality_update_count;
		quality_track_sid = publication != nullptr ? publication->Sid() : "";
		quality_event_is_local = participant != nullptr && participant->IsLocalParticipant();
		quality_update = update;
	}

	int unpublished_count = 0;
	std::string track_sid;
	bool participant_is_local = false;
	int subscribed_count = 0;
	std::string subscribed_track_sid;
	bool subscriber_event_is_local = false;
	int quality_update_count = 0;
	std::string quality_track_sid;
	bool quality_event_is_local = false;
	SubscribedQualityUpdate quality_update;
};

class ConnectionEvents final : public RoomEventInterface {
public:
	void OnConnected() override { ++connected_count; }
	void OnReconnecting() override { ++reconnecting_count; }
	void OnReconnected() override { ++reconnected_count; }
	void OnDisconnected() override { ++disconnected_count; }
	void OnConnectionStateChanged(RoomState state) override { states.push_back(state); }

	int connected_count = 0;
	int reconnecting_count = 0;
	int reconnected_count = 0;
	int disconnected_count = 0;
	std::vector<RoomState> states;
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
	ASSERT_EQ(events.states.size(), 1u);
	EXPECT_EQ(events.states[0], RoomState::Connected);

	room.ReconnectingEvent(false);
	EXPECT_EQ(room.State(), RoomInterface::RoomState::Reconnecting);
	EXPECT_FALSE(room.IsConnected());
	EXPECT_EQ(events.reconnecting_count, 1);
	ASSERT_EQ(events.states.size(), 2u);
	EXPECT_EQ(events.states[1], RoomState::Reconnecting);

	room.ResumedEvent();
	EXPECT_EQ(room.State(), RoomInterface::RoomState::Connected);
	EXPECT_TRUE(room.IsConnected());
	EXPECT_EQ(events.reconnected_count, 1);
	ASSERT_EQ(events.states.size(), 3u);
	EXPECT_EQ(events.states[2], RoomState::Connected);

	EXPECT_TRUE(room.Disconnect());
	EXPECT_EQ(room.LastDisconnectReason(), DisconnectReason::ClientInitiated);
	ASSERT_EQ(events.states.size(), 5u);
	EXPECT_EQ(events.states[3], RoomState::Disconnecting);
	EXPECT_EQ(events.states[4], RoomState::Disconnected);
	room.RemoveEventListener();
}

TEST(RoomConnectionStateTest, DoesNotDuplicateEventWhenResumeEscalatesToFullReconnect) {
	Room room;
	ConnectionEvents events;
	room.AddEventListener(&events);
	room.ConnectedEvent({});

	room.ReconnectingEvent(false);
	room.ReconnectingEvent(true);
	room.ReconnectingEvent(true);

	EXPECT_EQ(room.State(), RoomInterface::RoomState::Reconnecting);
	EXPECT_EQ(events.reconnecting_count, 1);
	ASSERT_EQ(events.states.size(), 2u);
	EXPECT_EQ(events.states[1], RoomState::Reconnecting);

	room.ResumedEvent();
	EXPECT_EQ(events.reconnected_count, 1);
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
	ASSERT_EQ(events.states.size(), 2u);
	EXPECT_EQ(events.states[0], RoomState::Connected);
	EXPECT_EQ(events.states[1], RoomState::Failed);

	room.SignalDisconnectedEvent(livekit::DisconnectReason::SIGNAL_CLOSE);
	EXPECT_EQ(events.disconnected_count, 1);
	EXPECT_TRUE(room.Disconnect());
	EXPECT_EQ(room.State(), RoomInterface::RoomState::Disconnected);
	EXPECT_EQ(events.disconnected_count, 1);
	ASSERT_EQ(events.states.size(), 4u);
	EXPECT_EQ(events.states[2], RoomState::Disconnecting);
	EXPECT_EQ(events.states[3], RoomState::Disconnected);
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
	void OnDataChannelBufferStatusChanged(const DataChannelBufferStatus& status) override {
		buffer_statuses.push_back(status);
	}
	void OnSipDtmfReceived(const SipDtmfEvent& event) override { dtmf_events.push_back(event); }
	void OnChatMessageReceived(const ChatMessage& message) override {
		chat_messages.push_back(message);
	}
	void OnTranscriptionReceived(const TranscriptionReceivedEvent& event) override {
		transcriptions.push_back(event);
	}
	void OnMetricsReceived(const MetricsReceivedEvent& event) override { metrics.push_back(event); }

	std::vector<TextReceivedEvent> texts;
	std::vector<ByteReceivedEvent> bytes;
	std::vector<FileReceivedEvent> files;
	std::vector<DataChannelBufferStatus> buffer_statuses;
	std::vector<SipDtmfEvent> dtmf_events;
	std::vector<ChatMessage> chat_messages;
	std::vector<TranscriptionReceivedEvent> transcriptions;
	std::vector<MetricsReceivedEvent> metrics;
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

livekit::DataPacket FailedStreamTrailer(const std::string& id, const std::string& reason) {
	auto packet = StreamTrailer(id);
	packet.mutable_stream_trailer()->set_reason(reason);
	return packet;
}

std::string DeflateRaw(const std::string& input) {
	detail::DeflateRawStream deflater;
	std::vector<uint8_t> output;
	if (!deflater.IsValid() ||
	    !deflater.Write(reinterpret_cast<const uint8_t*>(input.data()), input.size(), output) ||
	    !deflater.Finish(output)) {
		return {};
	}
	return {output.begin(), output.end()};
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

TEST(DataStreamStateTest, DecompressesRawDeflateStreamsAndRejectsInvalidPayloads) {
	Room room;
	DataStreamEvents events;
	room.AddEventListener(&events);
	std::vector<TextStreamEvent> failed_events;
	ASSERT_TRUE(room.RegisterTextStreamHandler(
	    "stream-topic", [&](const TextStreamEvent& event) { failed_events.push_back(event); }));

	const std::string text(32'000, 'a');
	const auto compressed_text = DeflateRaw(text);
	ASSERT_FALSE(compressed_text.empty());
	auto text_header = StreamHeader("compressed-text", text.size());
	text_header.mutable_stream_header()->mutable_text_header();
	text_header.mutable_stream_header()->set_compression(
	    livekit::DataStream_CompressionType_DEFLATE_RAW);
	room.DataPacketEvent(text_header);
	const auto split = compressed_text.size() / 2;
	room.DataPacketEvent(StreamChunk("compressed-text", 0, compressed_text.substr(0, split)));
	room.DataPacketEvent(StreamChunk("compressed-text", 1, compressed_text.substr(split)));
	room.DataPacketEvent(StreamTrailer("compressed-text"));
	ASSERT_GE(failed_events.size(), 4u);
	std::string received_text;
	for (const auto& event : failed_events) {
		if (event.type == DataStreamEventType::Chunk) {
			received_text += event.content;
		}
	}
	EXPECT_EQ(received_text, text);
	failed_events.clear();
	EXPECT_TRUE(room.UnregisterTextStreamHandler("stream-topic"));

	const std::string bytes(8'000, '\x5a');
	auto inline_bytes = StreamHeader("compressed-inline", bytes.size());
	inline_bytes.mutable_stream_header()->mutable_byte_header();
	inline_bytes.mutable_stream_header()->set_compression(
	    livekit::DataStream_CompressionType_DEFLATE_RAW);
	inline_bytes.mutable_stream_header()->set_inline_content(DeflateRaw(bytes));
	room.DataPacketEvent(inline_bytes);
	ASSERT_EQ(events.bytes.size(), 1u);
	EXPECT_EQ(events.bytes[0].data, std::vector<uint8_t>(bytes.begin(), bytes.end()));

	// Node.js zlib.deflateRawSync golden payload, matching the browser/JS SDK deflate-raw format.
	const uint8_t js_deflate_payload[] = {
	    0xf3, 0xc9, 0x2c, 0x4b, 0xf5, 0xce, 0x2c, 0x51, 0x48, 0x49, 0x4d, 0xcb,
	    0x49, 0x2c, 0x49, 0x55, 0xc8, 0xcc, 0x2b, 0x49, 0x2d, 0xca, 0x2f, 0x48,
	    0x2d, 0x4a, 0x4c, 0xca, 0xcc, 0xc9, 0x2c, 0xa9, 0x04, 0x00,
	};
	auto js_header = StreamHeader("js-deflate", 32);
	js_header.mutable_stream_header()->mutable_text_header();
	js_header.mutable_stream_header()->set_compression(
	    livekit::DataStream_CompressionType_DEFLATE_RAW);
	js_header.mutable_stream_header()->set_inline_content(js_deflate_payload,
	                                                      sizeof(js_deflate_payload));
	room.DataPacketEvent(js_header);
	ASSERT_EQ(events.texts.size(), 1u);
	EXPECT_EQ(events.texts[0].text, "LiveKit deflate interoperability");

	auto invalid_header = StreamHeader("invalid-compressed", 10);
	invalid_header.mutable_stream_header()->mutable_text_header();
	invalid_header.mutable_stream_header()->set_compression(
	    livekit::DataStream_CompressionType_DEFLATE_RAW);
	ASSERT_TRUE(room.RegisterTextStreamHandler(
	    "stream-topic", [&](const TextStreamEvent& event) { failed_events.push_back(event); }));
	room.DataPacketEvent(invalid_header);
	room.DataPacketEvent(StreamChunk("invalid-compressed", 0, "not deflate"));
	room.DataPacketEvent(StreamTrailer("invalid-compressed"));
	EXPECT_EQ(events.texts.size(), 1u);
	ASSERT_GE(failed_events.size(), 2u);
	EXPECT_EQ(failed_events.back().type, DataStreamEventType::Failed);
	EXPECT_EQ(failed_events.back().reason, "invalid stream chunk");
	EXPECT_TRUE(room.UnregisterTextStreamHandler("stream-topic"));

	auto oversized = StreamHeader("oversized-compressed", 64ULL * 1024 * 1024 + 1);
	oversized.mutable_stream_header()->mutable_byte_header();
	oversized.mutable_stream_header()->set_compression(
	    livekit::DataStream_CompressionType_DEFLATE_RAW);
	room.DataPacketEvent(oversized);
	room.DataPacketEvent(StreamChunk("oversized-compressed", 0, DeflateRaw("ignored")));
	room.DataPacketEvent(StreamTrailer("oversized-compressed"));
	EXPECT_EQ(events.bytes.size(), 1u);

	detail::InflateRawStream limited_inflater(1024);
	std::vector<uint8_t> limited_output;
	const auto compressed_large = DeflateRaw(std::string(2048, 'z'));
	EXPECT_FALSE(limited_inflater.Write(reinterpret_cast<const uint8_t*>(compressed_large.data()),
	                                    compressed_large.size(), limited_output));
	room.RemoveEventListener();
}

TEST(DataStreamStateTest, DispatchesRegisteredTopicsIncrementallyWithoutLegacyBuffering) {
	Room room;
	DataStreamEvents legacy_events;
	room.AddEventListener(&legacy_events);
	std::vector<TextStreamEvent> text_events;
	std::vector<ByteStreamEvent> byte_events;
	ASSERT_TRUE(room.RegisterTextStreamHandler(
	    "stream-topic", [&](const TextStreamEvent& event) { text_events.push_back(event); }));
	EXPECT_FALSE(room.RegisterTextStreamHandler("stream-topic", [](const TextStreamEvent&) {}));

	auto text_header = StreamHeader("incremental-text", 11);
	text_header.mutable_stream_header()->set_mime_type("text/plain");
	text_header.mutable_stream_header()->mutable_text_header();
	room.DataPacketEvent(text_header);
	room.DataPacketEvent(StreamChunk("incremental-text", 0, "hello "));
	room.DataPacketEvent(StreamChunk("incremental-text", 1, "world"));
	room.DataPacketEvent(StreamTrailer("incremental-text"));
	ASSERT_EQ(text_events.size(), 4u);
	EXPECT_EQ(text_events[0].type, DataStreamEventType::Open);
	EXPECT_EQ(text_events[0].info.participant_identity, "sender");
	EXPECT_EQ(text_events[1].content, "hello ");
	EXPECT_EQ(text_events[1].chunk_index, 0u);
	EXPECT_EQ(text_events[2].content, "world");
	EXPECT_EQ(text_events[3].type, DataStreamEventType::Closed);
	EXPECT_TRUE(legacy_events.texts.empty());
	EXPECT_TRUE(room.UnregisterTextStreamHandler("stream-topic"));
	EXPECT_FALSE(room.UnregisterTextStreamHandler("stream-topic"));

	ASSERT_TRUE(room.RegisterByteStreamHandler(
	    "stream-topic", [&](const ByteStreamEvent& event) { byte_events.push_back(event); }));
	auto bytes_header = StreamHeader("incremental-bytes", 4);
	bytes_header.mutable_stream_header()->set_mime_type("application/test");
	bytes_header.mutable_stream_header()->mutable_byte_header()->set_name("payload.bin");
	room.DataPacketEvent(bytes_header);
	room.DataPacketEvent(StreamChunk("incremental-bytes", 0, "data"));
	room.DataPacketEvent(FailedStreamTrailer("incremental-bytes", "sender cancelled"));
	ASSERT_EQ(byte_events.size(), 3u);
	EXPECT_EQ(byte_events[0].type, DataStreamEventType::Open);
	EXPECT_EQ(byte_events[0].info.name, "payload.bin");
	EXPECT_EQ(byte_events[1].content, std::vector<uint8_t>({'d', 'a', 't', 'a'}));
	EXPECT_EQ(byte_events[2].type, DataStreamEventType::Failed);
	EXPECT_EQ(byte_events[2].reason, "sender cancelled");
	EXPECT_TRUE(legacy_events.bytes.empty());
	EXPECT_TRUE(room.UnregisterByteStreamHandler("stream-topic"));
	room.RemoveEventListener();
}

TEST(DataStreamStateTest, FailsOpenTopicStreamsWhenRoomDisconnects) {
	Room room;
	std::vector<TextStreamEvent> events;
	ASSERT_TRUE(room.RegisterTextStreamHandler(
	    "stream-topic", [&](const TextStreamEvent& event) { events.push_back(event); }));
	auto header = StreamHeader("open-stream", 100);
	header.mutable_stream_header()->mutable_text_header();
	room.DataPacketEvent(header);
	ASSERT_EQ(events.size(), 1u);
	room.ConnectedEvent({});
	ASSERT_TRUE(room.Disconnect());
	ASSERT_EQ(events.size(), 2u);
	EXPECT_EQ(events.back().type, DataStreamEventType::Failed);
	EXPECT_EQ(events.back().reason, "room disconnected");
}

TEST(DataStreamStateTest, ForwardsDataChannelBackpressureTransitions) {
	Room room;
	DataStreamEvents events;
	room.AddEventListener(&events);
	room.DataChannelBufferStatusEvent({true, 4 * 1024 * 1024, 4 * 1024 * 1024, 1024 * 1024, true});
	ASSERT_EQ(events.buffer_statuses.size(), 1u);
	EXPECT_TRUE(events.buffer_statuses[0].reliable);
	EXPECT_TRUE(events.buffer_statuses[0].backpressured);
	EXPECT_EQ(events.buffer_statuses[0].high_water_mark, 4u * 1024 * 1024);
	room.RemoveEventListener();
}

TEST(DataStreamStateTest, ForwardsSipDtmfPackets) {
	Room room;
	DataStreamEvents events;
	room.AddEventListener(&events);
	livekit::DataPacket packet;
	packet.set_participant_identity("sip-participant");
	packet.mutable_sip_dtmf()->set_code(11);
	packet.mutable_sip_dtmf()->set_digit("#");
	room.DataPacketEvent(packet);
	ASSERT_EQ(events.dtmf_events.size(), 1u);
	EXPECT_EQ(events.dtmf_events[0].code, 11u);
	EXPECT_EQ(events.dtmf_events[0].digit, "#");
	EXPECT_EQ(events.dtmf_events[0].participant_identity, "sip-participant");
	room.RemoveEventListener();
}

TEST(DataStreamStateTest, ForwardsStructuredChatMessages) {
	Room room;
	DataStreamEvents events;
	room.AddEventListener(&events);
	livekit::DataPacket packet;
	packet.set_participant_identity("chat-participant");
	auto* chat = packet.mutable_chat_message();
	chat->set_id("message-id");
	chat->set_timestamp(1000);
	chat->set_edit_timestamp(2000);
	chat->set_message("edited text");
	chat->set_generated(true);
	room.DataPacketEvent(packet);
	ASSERT_EQ(events.chat_messages.size(), 1u);
	EXPECT_EQ(events.chat_messages[0].id, "message-id");
	EXPECT_EQ(events.chat_messages[0].timestamp, 1000);
	ASSERT_TRUE(events.chat_messages[0].edit_timestamp.has_value());
	EXPECT_EQ(*events.chat_messages[0].edit_timestamp, 2000);
	EXPECT_EQ(events.chat_messages[0].message, "edited text");
	EXPECT_TRUE(events.chat_messages[0].generated);
	EXPECT_EQ(events.chat_messages[0].participant_identity, "chat-participant");
	room.RemoveEventListener();
}

TEST(DataStreamStateTest, TracksIncrementalTranscriptionSegments) {
	Room room;
	DataStreamEvents events;
	room.AddEventListener(&events);
	livekit::DataPacket partial_packet;
	auto* partial = partial_packet.mutable_transcription();
	partial->set_transcribed_participant_identity("speaker");
	partial->set_track_id("TR_audio");
	auto* partial_segment = partial->add_segments();
	partial_segment->set_id("segment-id");
	partial_segment->set_text("partial text");
	partial_segment->set_language("en");
	partial_segment->set_start_time(100);
	partial_segment->set_end_time(200);
	room.DataPacketEvent(partial_packet);

	ASSERT_EQ(events.transcriptions.size(), 1u);
	ASSERT_EQ(events.transcriptions[0].segments.size(), 1u);
	const auto first_received_time = events.transcriptions[0].segments[0].first_received_time;
	EXPECT_EQ(events.transcriptions[0].transcribed_participant_identity, "speaker");
	EXPECT_EQ(events.transcriptions[0].track_id, "TR_audio");
	EXPECT_EQ(events.transcriptions[0].segments[0].id, "segment-id");
	EXPECT_EQ(events.transcriptions[0].segments[0].text, "partial text");
	EXPECT_EQ(events.transcriptions[0].segments[0].language, "en");
	EXPECT_EQ(events.transcriptions[0].segments[0].start_time, 100u);
	EXPECT_EQ(events.transcriptions[0].segments[0].end_time, 200u);
	EXPECT_FALSE(events.transcriptions[0].segments[0].final);
	EXPECT_GT(first_received_time, 0);
	EXPECT_EQ(events.transcriptions[0].segments[0].last_received_time, first_received_time);

	livekit::DataPacket final_packet;
	auto* final_transcription = final_packet.mutable_transcription();
	final_transcription->set_transcribed_participant_identity("speaker");
	final_transcription->set_track_id("TR_audio");
	auto* final_segment = final_transcription->add_segments();
	final_segment->set_id("segment-id");
	final_segment->set_text("final text");
	final_segment->set_language("en");
	final_segment->set_start_time(100);
	final_segment->set_end_time(250);
	final_segment->set_final(true);
	room.DataPacketEvent(final_packet);

	ASSERT_EQ(events.transcriptions.size(), 2u);
	ASSERT_EQ(events.transcriptions[1].segments.size(), 1u);
	EXPECT_EQ(events.transcriptions[1].segments[0].text, "final text");
	EXPECT_TRUE(events.transcriptions[1].segments[0].final);
	EXPECT_EQ(events.transcriptions[1].segments[0].first_received_time, first_received_time);
	EXPECT_GE(events.transcriptions[1].segments[0].last_received_time, first_received_time);
	room.RemoveEventListener();
}

TEST(DataStreamStateTest, ForwardsStructuredMetricsBatches) {
	Room room;
	DataStreamEvents events;
	room.AddEventListener(&events);
	livekit::DataPacket packet;
	packet.set_participant_identity("metrics-sender");
	auto* metrics = packet.mutable_metrics();
	metrics->set_timestamp_ms(1234);
	metrics->mutable_normalized_timestamp()->set_seconds(100);
	metrics->mutable_normalized_timestamp()->set_nanos(200);
	metrics->add_str_data("participant");
	metrics->add_str_data("track");
	metrics->add_str_data("rid");
	auto* series = metrics->add_time_series();
	series->set_label(17);
	series->set_participant_identity(4096);
	series->set_track_sid(4097);
	series->set_rid(4098);
	auto* sample = series->add_samples();
	sample->set_timestamp_ms(1200);
	sample->mutable_normalized_timestamp()->set_seconds(99);
	sample->mutable_normalized_timestamp()->set_nanos(50);
	sample->set_value(42.5f);
	auto* metric_event = metrics->add_events();
	metric_event->set_label(3);
	metric_event->set_participant_identity(4096);
	metric_event->set_track_sid(4097);
	metric_event->set_rid(4098);
	metric_event->set_start_timestamp_ms(1100);
	metric_event->set_end_timestamp_ms(1300);
	metric_event->mutable_normalized_start_timestamp()->set_seconds(98);
	metric_event->mutable_normalized_end_timestamp()->set_seconds(101);
	metric_event->set_metadata("{\"reason\":\"test\"}");
	room.DataPacketEvent(packet);

	ASSERT_EQ(events.metrics.size(), 1u);
	const auto& received = events.metrics[0];
	EXPECT_EQ(received.timestamp_ms, 1234);
	ASSERT_TRUE(received.normalized_timestamp.has_value());
	EXPECT_EQ(received.normalized_timestamp->seconds, 100);
	EXPECT_EQ(received.normalized_timestamp->nanos, 200);
	EXPECT_EQ(received.participant_identity, "metrics-sender");
	ASSERT_EQ(received.string_data.size(), 3u);
	EXPECT_EQ(received.string_data[0], "participant");
	ASSERT_EQ(received.time_series.size(), 1u);
	EXPECT_EQ(received.time_series[0].label, 17u);
	EXPECT_EQ(received.time_series[0].participant_identity, 4096u);
	EXPECT_EQ(received.time_series[0].track_sid, 4097u);
	EXPECT_EQ(received.time_series[0].rid, 4098u);
	ASSERT_EQ(received.time_series[0].samples.size(), 1u);
	EXPECT_EQ(received.time_series[0].samples[0].timestamp_ms, 1200);
	ASSERT_TRUE(received.time_series[0].samples[0].normalized_timestamp.has_value());
	EXPECT_EQ(received.time_series[0].samples[0].normalized_timestamp->seconds, 99);
	EXPECT_FLOAT_EQ(received.time_series[0].samples[0].value, 42.5f);
	ASSERT_EQ(received.events.size(), 1u);
	EXPECT_EQ(received.events[0].label, 3u);
	EXPECT_EQ(received.events[0].start_timestamp_ms, 1100);
	ASSERT_TRUE(received.events[0].end_timestamp_ms.has_value());
	EXPECT_EQ(*received.events[0].end_timestamp_ms, 1300);
	ASSERT_TRUE(received.events[0].normalized_start_timestamp.has_value());
	ASSERT_TRUE(received.events[0].normalized_end_timestamp.has_value());
	EXPECT_EQ(received.events[0].metadata, "{\"reason\":\"test\"}");
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

TEST(LocalTrackStateTest, ForwardsFirstRemoteSubscriptionOnce) {
	Room room;
	LocalTrackEvents events;
	room.AddEventListener(&events);
	auto* participant = dynamic_cast<LocalParticipant*>(room.GetLocalParticipant());
	ASSERT_NE(participant, nullptr);

	room.LocalTrackSubscribedEvent("TR_pending");
	EXPECT_EQ(events.subscribed_count, 0);
	livekit::TrackInfo info = MakeTrack("TR_local", "camera", livekit::TrackType::VIDEO,
	                                    livekit::TrackSource::CAMERA, false);
	auto publication = std::make_shared<TrackPublication>(info, nullptr);
	participant->AddTrackPublication(publication);

	room.LocalTrackSubscribedEvent("TR_local");
	EXPECT_EQ(events.subscribed_count, 1);
	EXPECT_EQ(events.subscribed_track_sid, "TR_local");
	EXPECT_TRUE(events.subscriber_event_is_local);
	room.LocalTrackSubscribedEvent("TR_local");
	EXPECT_EQ(events.subscribed_count, 1);
	room.RemoveEventListener();
}

TEST(LocalTrackStateTest, RetainsAndForwardsSubscribedQualityUpdates) {
	Room room;
	LocalTrackEvents events;
	room.AddEventListener(&events);
	auto* participant = dynamic_cast<LocalParticipant*>(room.GetLocalParticipant());
	ASSERT_NE(participant, nullptr);

	livekit::TrackInfo info = MakeTrack("TR_video", "camera", livekit::TrackType::VIDEO,
	                                    livekit::TrackSource::CAMERA, false);
	auto publication = std::make_shared<LocalTrackPublication>(info, nullptr);
	participant->AddTrackPublication(publication);

	livekit::SubscribedQualityUpdate update;
	update.set_track_sid("TR_video");
	auto* legacy = update.add_subscribed_qualities();
	legacy->set_quality(livekit::VideoQuality::HIGH);
	legacy->set_enabled(true);
	auto* codec = update.add_subscribed_codecs();
	codec->set_codec("video/VP8");
	auto* low = codec->add_qualities();
	low->set_quality(livekit::VideoQuality::LOW);
	low->set_enabled(false);
	auto* high = codec->add_qualities();
	high->set_quality(livekit::VideoQuality::HIGH);
	high->set_enabled(true);
	room.SubscribedQualityUpdateEvent(update);

	EXPECT_EQ(events.quality_update_count, 1);
	EXPECT_EQ(events.quality_track_sid, "TR_video");
	EXPECT_TRUE(events.quality_event_is_local);
	EXPECT_EQ(events.quality_update.track_sid, "TR_video");
	ASSERT_EQ(events.quality_update.qualities.size(), 1u);
	EXPECT_EQ(events.quality_update.qualities[0].quality, VideoQuality::High);
	EXPECT_TRUE(events.quality_update.qualities[0].enabled);
	ASSERT_EQ(events.quality_update.codecs.size(), 1u);
	EXPECT_EQ(events.quality_update.codecs[0].codec, "video/VP8");
	ASSERT_EQ(events.quality_update.codecs[0].qualities.size(), 2u);
	EXPECT_EQ(events.quality_update.codecs[0].qualities[0].quality, VideoQuality::Low);
	EXPECT_FALSE(events.quality_update.codecs[0].qualities[0].enabled);

	const auto retained = publication->LastSubscribedQualityUpdate();
	ASSERT_TRUE(retained.has_value());
	EXPECT_EQ(retained->track_sid, "TR_video");
	EXPECT_EQ(retained->codecs[0].qualities[1].quality, VideoQuality::High);
	EXPECT_TRUE(retained->codecs[0].qualities[1].enabled);

	update.set_track_sid("TR_unknown");
	room.SubscribedQualityUpdateEvent(update);
	EXPECT_EQ(events.quality_update_count, 1);
	room.RemoveEventListener();
}

} // namespace
} // namespace livekit::core
