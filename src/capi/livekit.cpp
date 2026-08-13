#include "livekit/capi/livekit.h"

#include "livekit/core/livekit_client.h"
#include "livekit/core/rpc.h"
#include "livekit/core/track/audio_source_interface.h"
#include "livekit/core/track/local_track_interface.h"
#include "livekit/core/track/remote_track_interface.h"
#include "livekit/core/track/video_source_interface.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace core = livekit::core;

class CRoomEvents;

struct RoomHandleState {
	std::atomic_bool alive{true};
	std::atomic_bool connected{false};
};

struct lk_room {
	std::unique_ptr<core::RoomInterface> room;
	std::unique_ptr<CRoomEvents> events;
	std::mutex callbacks_mutex;
	std::mutex callback_lifetime_mutex;
	std::mutex local_tracks_mutex;
	std::vector<lk_local_track_t*> local_tracks;
	lk_room_callbacks_t callbacks{};
	std::shared_ptr<RoomHandleState> state = std::make_shared<RoomHandleState>();
};

struct lk_audio_source {
	std::unique_ptr<core::AudioSourceInterface> source;
	std::atomic_size_t track_references{0};
	uint32_t sample_rate = 0;
	uint32_t num_channels = 0;
};

struct lk_video_source {
	std::unique_ptr<core::VideoSourceInterface> source;
	std::atomic_size_t track_references{0};
};

struct lk_local_track {
	std::unique_ptr<core::LocalTrackInterface> track;
	lk_room_t* owner = nullptr;
	lk_audio_source_t* audio_source = nullptr;
	lk_video_source_t* video_source = nullptr;
	std::shared_ptr<RoomHandleState> room_state;
	std::atomic_bool published{false};
};

struct lk_rpc_result {
	core::RpcResult result;
};

namespace {

thread_local std::string last_error;

void SetError(std::string error) { last_error = std::move(error); }

lk_status_t Failure(lk_status_t status, const char* message) {
	SetError(message);
	return status;
}

template <typename Function> lk_status_t Guard(Function&& function) noexcept {
	try {
		last_error.clear();
		return function();
	} catch (const std::exception& exception) {
		SetError(exception.what());
		return LK_STATUS_EXCEPTION;
	} catch (...) {
		SetError("unknown C++ exception");
		return LK_STATUS_EXCEPTION;
	}
}

template <typename Function> size_t SizeGuard(Function&& function) noexcept {
	try {
		last_error.clear();
		return function();
	} catch (const std::exception& exception) {
		SetError(exception.what());
		return 0;
	} catch (...) {
		SetError("unknown C++ exception");
		return 0;
	}
}

size_t CopyString(const std::string& value, char* buffer, size_t buffer_size) noexcept {
	const size_t required = value.size() + 1;
	if (buffer != nullptr && buffer_size != 0) {
		const size_t count = std::min(value.size(), buffer_size - 1);
		std::memcpy(buffer, value.data(), count);
		buffer[count] = '\0';
	}
	return required;
}

bool HasField(size_t struct_size, size_t offset, size_t field_size) {
	return struct_size >= offset + field_size;
}

#define LKC_HAS_FIELD(value, type, field)                                                          \
	HasField((value)->struct_size, offsetof(type, field), sizeof((value)->field))

core::TrackSource ToCoreTrackSource(lk_track_source_t source) {
	switch (source) {
	case LK_TRACK_SOURCE_CAMERA:
		return core::TrackSource::Camera;
	case LK_TRACK_SOURCE_MICROPHONE:
		return core::TrackSource::Microphone;
	case LK_TRACK_SOURCE_SCREEN_SHARE:
		return core::TrackSource::ScreenShare;
	case LK_TRACK_SOURCE_SCREEN_SHARE_AUDIO:
		return core::TrackSource::ScreenShareAudio;
	default:
		return core::TrackSource::Unknown;
	}
}

lk_track_kind_t ToCTrackKind(core::TrackKind kind) {
	switch (kind) {
	case core::TrackKind::Audio:
		return LK_TRACK_KIND_AUDIO;
	case core::TrackKind::Video:
		return LK_TRACK_KIND_VIDEO;
	default:
		return LK_TRACK_KIND_UNKNOWN;
	}
}

lk_track_source_t ToCTrackSource(core::TrackSource source) {
	switch (source) {
	case core::TrackSource::Camera:
		return LK_TRACK_SOURCE_CAMERA;
	case core::TrackSource::Microphone:
		return LK_TRACK_SOURCE_MICROPHONE;
	case core::TrackSource::ScreenShare:
		return LK_TRACK_SOURCE_SCREEN_SHARE;
	case core::TrackSource::ScreenShareAudio:
		return LK_TRACK_SOURCE_SCREEN_SHARE_AUDIO;
	default:
		return LK_TRACK_SOURCE_UNKNOWN;
	}
}

lk_connection_quality_t ToCConnectionQuality(core::ConnectionQuality quality) {
	switch (quality) {
	case core::ConnectionQuality::Poor:
		return LK_CONNECTION_QUALITY_POOR;
	case core::ConnectionQuality::Good:
		return LK_CONNECTION_QUALITY_GOOD;
	case core::ConnectionQuality::Excellent:
		return LK_CONNECTION_QUALITY_EXCELLENT;
	case core::ConnectionQuality::Lost:
		return LK_CONNECTION_QUALITY_LOST;
	default:
		return LK_CONNECTION_QUALITY_UNKNOWN;
	}
}

lk_subscription_error_t ToCSubscriptionError(core::SubscriptionError error) {
	switch (error) {
	case core::SubscriptionError::CodecUnsupported:
		return LK_SUBSCRIPTION_ERROR_CODEC_UNSUPPORTED;
	case core::SubscriptionError::TrackNotFound:
		return LK_SUBSCRIPTION_ERROR_TRACK_NOT_FOUND;
	default:
		return LK_SUBSCRIPTION_ERROR_UNKNOWN;
	}
}

lk_room_state_t ToCRoomState(core::RoomInterface::RoomState state) {
	switch (state) {
	case core::RoomInterface::RoomState::Connecting:
		return LK_ROOM_STATE_CONNECTING;
	case core::RoomInterface::RoomState::Connected:
		return LK_ROOM_STATE_CONNECTED;
	case core::RoomInterface::RoomState::Disconnecting:
		return LK_ROOM_STATE_DISCONNECTING;
	case core::RoomInterface::RoomState::Failed:
		return LK_ROOM_STATE_FAILED;
	case core::RoomInterface::RoomState::Reconnecting:
		return LK_ROOM_STATE_RECONNECTING;
	default:
		return LK_ROOM_STATE_DISCONNECTED;
	}
}

lk_disconnect_reason_t ToCDisconnectReason(core::DisconnectReason reason) {
	return static_cast<lk_disconnect_reason_t>(reason);
}

struct OwnedParticipantInfo {
	explicit OwnedParticipantInfo(core::ParticipantInterface* participant) {
		if (participant == nullptr) {
			return;
		}
		sid = participant->Sid();
		identity = participant->Identity();
		name = participant->Name();
		metadata = participant->Metadata();
		info = {sid.c_str(),
		        identity.c_str(),
		        name.c_str(),
		        metadata.c_str(),
		        participant->AudioLevel(),
		        ToCConnectionQuality(participant->GetConnectionQuality()),
		        participant->IsSpeaking() ? 1 : 0,
		        participant->IsLocalParticipant() ? 1 : 0};
	}

	std::string sid;
	std::string identity;
	std::string name;
	std::string metadata;
	lk_participant_info_t info{};
};

struct OwnedTrackInfo {
	explicit OwnedTrackInfo(core::TrackPublicationInterface* publication) {
		if (publication == nullptr) {
			return;
		}
		sid = publication->Sid();
		name = publication->Name();
		mime_type = publication->MimeType();
		const auto dimensions = publication->Dimensions();
		info = {sid.c_str(),
		        name.c_str(),
		        mime_type.c_str(),
		        ToCTrackKind(publication->Kind()),
		        ToCTrackSource(publication->Source()),
		        dimensions.width,
		        dimensions.height,
		        publication->IsMuted() ? 1 : 0,
		        publication->IsSimulcasted() ? 1 : 0,
		        publication->IsSubscriptionAllowed() ? 1 : 0};
	}

	OwnedTrackInfo(core::RemoteTrackInterface* track,
	               core::RemoteParticipantInterface* participant) {
		core::TrackPublicationInterface* publication = nullptr;
		if (track != nullptr && participant != nullptr) {
			for (auto* candidate : participant->GetTrackPublications()) {
				if (candidate->Sid() == track->Sid()) {
					publication = candidate;
					break;
				}
			}
		}
		if (publication != nullptr) {
			sid = publication->Sid();
			name = publication->Name();
			mime_type = publication->MimeType();
			const auto dimensions = publication->Dimensions();
			info = {sid.c_str(),
			        name.c_str(),
			        mime_type.c_str(),
			        ToCTrackKind(publication->Kind()),
			        ToCTrackSource(publication->Source()),
			        dimensions.width,
			        dimensions.height,
			        publication->IsMuted() ? 1 : 0,
			        publication->IsSimulcasted() ? 1 : 0,
			        publication->IsSubscriptionAllowed() ? 1 : 0};
			return;
		}
		if (track == nullptr) {
			return;
		}
		sid = track->Sid();
		name = track->Name();
		const auto dimensions = track->Dimensions();
		info = {sid.c_str(),
		        name.c_str(),
		        "",
		        ToCTrackKind(track->Kind()),
		        ToCTrackSource(track->Source()),
		        dimensions.width,
		        dimensions.height,
		        0,
		        0,
		        1};
	}

	std::string sid;
	std::string name;
	std::string mime_type;
	lk_track_publication_info_t info{};
};

core::LocalParticipantInterface* LocalParticipant(lk_room_t* room) {
	return room != nullptr && room->room != nullptr ? room->room->GetLocalParticipant() : nullptr;
}

core::LocalParticipantInterface* LocalParticipant(const lk_room_t* room) {
	return room != nullptr && room->room != nullptr ? room->room->GetLocalParticipant() : nullptr;
}

template <typename Function> void InvokeRoomCallback(lk_room_t* owner, Function&& function) {
	if (owner == nullptr) {
		return;
	}
	std::lock_guard<std::mutex> lifetime_guard(owner->callback_lifetime_mutex);
	lk_room_callbacks_t callbacks{};
	{
		std::lock_guard<std::mutex> guard(owner->callbacks_mutex);
		callbacks = owner->callbacks;
	}
	function(callbacks);
}

std::vector<std::string> DestinationIdentities(const char* const* identities, size_t count) {
	std::vector<std::string> result;
	result.reserve(count);
	for (size_t index = 0; index < count; ++index) {
		if (identities[index] != nullptr) {
			result.emplace_back(identities[index]);
		}
	}
	return result;
}

bool CopyAttributes(const lk_attribute_t* attributes, size_t count,
                    std::map<std::string, std::string>& result) {
	if (count != 0 && attributes == nullptr) {
		return false;
	}
	for (size_t index = 0; index < count; ++index) {
		if (attributes[index].key == nullptr || attributes[index].value == nullptr) {
			return false;
		}
		result[attributes[index].key] = attributes[index].value;
	}
	return true;
}

} // namespace

class CRoomEvents final : public core::RoomEventInterface {
public:
	explicit CRoomEvents(lk_room_t* owner) : owner_(owner) {}

	void OnConnected() override {
		InvokeRoomCallback(owner_, [this](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_connected != nullptr) {
				callbacks.on_connected(callbacks.user_data, owner_);
			}
		});
	}

	void OnDisconnected() override { OnDisconnected(core::DisconnectReason::Unknown); }

	void OnDisconnected(core::DisconnectReason reason) override {
		owner_->state->connected.store(false);
		InvokeRoomCallback(owner_, [this, reason](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_disconnected != nullptr) {
				callbacks.on_disconnected(callbacks.user_data, owner_);
			}
			if (callbacks.on_disconnected_with_reason != nullptr) {
				callbacks.on_disconnected_with_reason(callbacks.user_data, owner_,
				                                      ToCDisconnectReason(reason));
			}
		});
	}

	void OnReconnecting() override {
		owner_->state->connected.store(false);
		InvokeRoomCallback(owner_, [this](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_reconnecting != nullptr) {
				callbacks.on_reconnecting(callbacks.user_data, owner_);
			}
		});
	}

	void OnReconnected() override {
		owner_->state->connected.store(true);
		InvokeRoomCallback(owner_, [this](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_reconnected != nullptr) {
				callbacks.on_reconnected(callbacks.user_data, owner_);
			}
		});
	}

	void OnParticipantConnected(core::RemoteParticipantInterface* participant) override {
		Participant(callbacks_member(&lk_room_callbacks_t::on_participant_connected), participant);
	}

	void OnParticipantDisconnected(core::RemoteParticipantInterface* participant) override {
		Participant(callbacks_member(&lk_room_callbacks_t::on_participant_disconnected),
		            participant);
	}

	void OnTrackPublished(core::TrackPublicationInterface* track,
	                      core::RemoteParticipantInterface* participant) override {
		Track(callbacks_member(&lk_room_callbacks_t::on_track_published), track, participant);
	}

	void OnTrackUnpublished(core::TrackPublicationInterface* track,
	                        core::RemoteParticipantInterface* participant) override {
		Track(callbacks_member(&lk_room_callbacks_t::on_track_unpublished), track, participant);
	}

	void OnLocalTrackPublished(core::TrackPublicationInterface* track,
	                           core::ParticipantInterface* participant) override {
		MarkLocalTrackPublished(track, true);
		Track(callbacks_member(&lk_room_callbacks_t::on_local_track_published), track, participant);
	}

	void OnLocalTrackUnpublished(core::TrackPublicationInterface* track,
	                             core::ParticipantInterface* participant) override {
		MarkLocalTrackPublished(track, false);
		Track(callbacks_member(&lk_room_callbacks_t::on_local_track_unpublished), track,
		      participant);
	}

	void OnTrackMuted(core::TrackPublicationInterface* track,
	                  core::ParticipantInterface* participant) override {
		Track(callbacks_member(&lk_room_callbacks_t::on_track_muted), track, participant);
	}

	void OnTrackUnmuted(core::TrackPublicationInterface* track,
	                    core::ParticipantInterface* participant) override {
		Track(callbacks_member(&lk_room_callbacks_t::on_track_unmuted), track, participant);
	}

	void OnTrackSubscribed(core::RemoteTrackInterface* track,
	                       core::RemoteParticipantInterface* participant) override {
		OwnedTrackInfo owned_track(track, participant);
		OwnedParticipantInfo owned_participant(participant);
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_track_subscribed != nullptr) {
				callbacks.on_track_subscribed(callbacks.user_data, owner_, &owned_track.info,
				                              &owned_participant.info);
			}
		});
	}

	void OnTrackSubscriptionFailed(const std::string& track_sid,
	                               core::RemoteParticipantInterface* participant,
	                               core::SubscriptionError error) override {
		core::TrackPublicationInterface* publication = nullptr;
		if (participant != nullptr) {
			for (auto* candidate : participant->GetTrackPublications()) {
				if (candidate->Sid() == track_sid) {
					publication = candidate;
					break;
				}
			}
		}
		OwnedTrackInfo owned_track(publication);
		OwnedParticipantInfo owned_participant(participant);
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_track_subscription_failed != nullptr) {
				callbacks.on_track_subscription_failed(callbacks.user_data, owner_,
				                                       &owned_track.info, &owned_participant.info,
				                                       ToCSubscriptionError(error));
			}
		});
	}

	void OnTrackSubscriptionPermissionChanged(core::TrackPublicationInterface* track,
	                                          core::RemoteParticipantInterface* participant,
	                                          bool allowed) override {
		OwnedTrackInfo owned_track(track);
		OwnedParticipantInfo owned_participant(participant);
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_track_subscription_permission_changed != nullptr) {
				callbacks.on_track_subscription_permission_changed(
				    callbacks.user_data, owner_, &owned_track.info, &owned_participant.info,
				    allowed ? 1 : 0);
			}
		});
	}

	void OnAudioFrame(core::RemoteTrackInterface* track,
	                  core::RemoteParticipantInterface* participant,
	                  const core::AudioFrame& frame) override {
		OwnedTrackInfo owned_track(track, participant);
		OwnedParticipantInfo owned_participant(participant);
		const lk_audio_frame_t c_frame{frame.data.data(), frame.data.size(), frame.sample_rate,
		                               frame.num_channels, frame.samples_per_channel};
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_audio_frame != nullptr) {
				callbacks.on_audio_frame(callbacks.user_data, owner_, &owned_track.info,
				                         &owned_participant.info, &c_frame);
			}
		});
	}

	void OnVideoFrame(core::RemoteTrackInterface* track,
	                  core::RemoteParticipantInterface* participant,
	                  const core::VideoFrame& frame) override {
		OwnedTrackInfo owned_track(track, participant);
		OwnedParticipantInfo owned_participant(participant);
		const lk_video_frame_t c_frame{frame.data.data(), frame.data.size(), frame.width,
		                               frame.height, frame.timestamp_us};
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_video_frame != nullptr) {
				callbacks.on_video_frame(callbacks.user_data, owner_, &owned_track.info,
				                         &owned_participant.info, &c_frame);
			}
		});
	}

	void OnDataReceived(const core::DataReceivedEvent& event) override {
		const lk_data_received_t c_event{event.payload.data(), event.payload.size(),
		                                 event.topic.c_str(), event.participant_identity.c_str(),
		                                 event.reliable ? 1 : 0};
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_data_received != nullptr) {
				callbacks.on_data_received(callbacks.user_data, owner_, &c_event);
			}
		});
	}

	void OnTextReceived(const core::TextReceivedEvent& event) override {
		const lk_text_received_t c_event{event.stream_id.c_str(),
		                                 event.text.c_str(),
		                                 event.topic.c_str(),
		                                 event.participant_identity.c_str(),
		                                 event.reply_to_stream_id.c_str(),
		                                 event.timestamp};
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_text_received != nullptr) {
				callbacks.on_text_received(callbacks.user_data, owner_, &c_event);
			}
		});
	}

	void OnByteReceived(const core::ByteReceivedEvent& event) override {
		File(callbacks_member(&lk_room_callbacks_t::on_byte_received), event);
	}

	void OnFileReceived(const core::FileReceivedEvent& event) override {
		File(callbacks_member(&lk_room_callbacks_t::on_file_received), event);
	}

private:
	void File(lk_file_received_callback lk_room_callbacks_t::*callback,
	          const core::FileReceivedEvent& event) {
		const lk_file_received_t c_event{event.data.data(),
		                                 event.data.size(),
		                                 event.stream_id.c_str(),
		                                 event.name.c_str(),
		                                 event.mime_type.c_str(),
		                                 event.topic.c_str(),
		                                 event.participant_identity.c_str()};
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.*callback != nullptr) {
				(callbacks.*callback)(callbacks.user_data, owner_, &c_event);
			}
		});
	}

public:
	void OnRoomMetadataChanged(const std::string& metadata) override {
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_room_metadata_changed != nullptr) {
				callbacks.on_room_metadata_changed(callbacks.user_data, owner_, metadata.c_str());
			}
		});
	}

	void OnConnectionQualityChanged(core::ConnectionQuality quality,
	                                core::ParticipantInterface* participant) override {
		OwnedParticipantInfo owned_participant(participant);
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_connection_quality_changed != nullptr) {
				callbacks.on_connection_quality_changed(callbacks.user_data, owner_,
				                                        ToCConnectionQuality(quality),
				                                        &owned_participant.info);
			}
		});
	}

	void
	OnActiveSpeakersChanged(const std::vector<core::ParticipantInterface*>& participants) override {
		std::vector<OwnedParticipantInfo> owned;
		owned.reserve(participants.size());
		for (auto* participant : participants) {
			owned.emplace_back(participant);
		}
		std::vector<lk_participant_info_t> infos;
		infos.reserve(owned.size());
		for (const auto& participant : owned) {
			infos.push_back(participant.info);
		}
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.on_active_speakers_changed != nullptr) {
				callbacks.on_active_speakers_changed(callbacks.user_data, owner_, infos.data(),
				                                     infos.size());
			}
		});
	}

private:
	template <typename Member> static Member callbacks_member(Member member) { return member; }

	void MarkLocalTrackPublished(core::TrackPublicationInterface* publication, bool published) {
		if (owner_ == nullptr || publication == nullptr) {
			return;
		}
		auto* local_track = publication->Track();
		std::lock_guard<std::mutex> guard(owner_->local_tracks_mutex);
		for (auto* handle : owner_->local_tracks) {
			if (handle != nullptr && handle->track.get() == local_track) {
				handle->published.store(published);
				break;
			}
		}
	}

	void Participant(lk_participant_event_callback lk_room_callbacks_t::*callback,
	                 core::ParticipantInterface* participant) {
		OwnedParticipantInfo owned(participant);
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.*callback != nullptr) {
				(callbacks.*callback)(callbacks.user_data, owner_, &owned.info);
			}
		});
	}

	void Track(lk_track_event_callback lk_room_callbacks_t::*callback,
	           core::TrackPublicationInterface* track, core::ParticipantInterface* participant) {
		OwnedTrackInfo owned_track(track);
		OwnedParticipantInfo owned_participant(participant);
		InvokeRoomCallback(owner_, [&](const lk_room_callbacks_t& callbacks) {
			if (callbacks.*callback != nullptr) {
				(callbacks.*callback)(callbacks.user_data, owner_, &owned_track.info,
				                      &owned_participant.info);
			}
		});
	}

	lk_room_t* owner_;
};

extern "C" {

lk_status_t lk_init(void) {
	return Guard([] { return core::Init() ? LK_STATUS_OK : LK_STATUS_OPERATION_FAILED; });
}

lk_status_t lk_shutdown(void) {
	return Guard([] { return core::Destroy() ? LK_STATUS_OK : LK_STATUS_OPERATION_FAILED; });
}

size_t lk_version(char* buffer, size_t buffer_size) {
	try {
		last_error.clear();
		return CopyString(core::Version(), buffer, buffer_size);
	} catch (const std::exception& exception) {
		SetError(exception.what());
		return 0;
	} catch (...) {
		SetError("unknown C++ exception");
		return 0;
	}
}

const char* lk_last_error(void) { return last_error.c_str(); }

void lk_room_callbacks_init(lk_room_callbacks_t* callbacks) {
	if (callbacks != nullptr) {
		*callbacks = {};
		callbacks->struct_size = sizeof(*callbacks);
	}
}

void lk_audio_source_options_init(lk_audio_source_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->sample_rate = 48000;
		options->num_channels = 1;
		options->queue_size_ms = 200;
	}
}

void lk_video_source_options_init(lk_video_source_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
	}
}

void lk_track_publish_options_init(lk_track_publish_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->dtx = 1;
		options->red = 1;
		options->simulcast = 1;
	}
}

void lk_data_publish_options_init(lk_data_publish_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->reliable = 1;
	}
}

void lk_file_send_options_init(lk_file_send_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->topic = "files";
		options->mime_type = "application/octet-stream";
		options->chunk_size = 15000;
	}
}

void lk_text_send_options_init(lk_text_send_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->chunk_size = 15000;
	}
}

void lk_byte_send_options_init(lk_byte_send_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->mime_type = "application/octet-stream";
		options->chunk_size = 15000;
	}
}

void lk_rpc_perform_options_init(lk_rpc_perform_options_t* options) {
	if (options != nullptr) {
		*options = {};
		options->struct_size = sizeof(*options);
		options->response_timeout_ms = 15000;
	}
}

void lk_participant_track_permission_init(lk_participant_track_permission_t* permission) {
	if (permission != nullptr) {
		*permission = {};
		permission->struct_size = sizeof(*permission);
	}
}

lk_status_t lk_room_create(lk_room_t** room) {
	return Guard([&] {
		if (room == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room output is null");
		}
		*room = nullptr;
		auto result = std::make_unique<lk_room_t>();
		result->room = core::CreateRoomUnique();
		if (!result->room) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to create room");
		}
		result->events = std::make_unique<CRoomEvents>(result.get());
		result->room->AddEventListener(result->events.get());
		*room = result.release();
		return LK_STATUS_OK;
	});
}

void lk_room_destroy(lk_room_t* room) {
	if (room == nullptr) {
		return;
	}
	try {
		room->room->RemoveEventListener();
		if (room->room->IsConnected()) {
			room->room->Disconnect();
		}
		room->state->connected.store(false);
		room->state->alive.store(false);
		{
			std::lock_guard<std::mutex> guard(room->local_tracks_mutex);
			for (auto* track : room->local_tracks) {
				if (track != nullptr) {
					track->owner = nullptr;
				}
			}
			room->local_tracks.clear();
		}
		{ std::lock_guard<std::mutex> guard(room->callback_lifetime_mutex); }
		delete room;
	} catch (...) {
		SetError("exception while destroying room");
	}
}

lk_status_t lk_room_set_callbacks(lk_room_t* room, const lk_room_callbacks_t* callbacks) {
	return Guard([&] {
		if (room == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room is null");
		}
		lk_room_callbacks_t copy{};
		if (callbacks != nullptr) {
			if (callbacks->struct_size < sizeof(callbacks->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid callbacks struct size");
			}
			std::memcpy(&copy, callbacks, std::min(callbacks->struct_size, sizeof(copy)));
			copy.struct_size = sizeof(copy);
		}
		std::lock_guard<std::mutex> guard(room->callbacks_mutex);
		room->callbacks = copy;
		return LK_STATUS_OK;
	});
}

lk_status_t lk_room_connect(lk_room_t* room, const char* url, const char* token) {
	return Guard([&] {
		if (room == nullptr || url == nullptr || token == nullptr || *url == '\0' ||
		    *token == '\0') {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room, URL, and token are required");
		}
		if (!room->room->Connect(url, token)) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to connect room");
		}
		room->state->connected.store(true);
		return LK_STATUS_OK;
	});
}

lk_status_t lk_room_disconnect(lk_room_t* room) {
	return Guard([&] {
		if (room == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room is null");
		}
		if (!room->room->Disconnect()) {
			return Failure(LK_STATUS_INVALID_STATE, "room is already disconnected");
		}
		room->state->connected.store(false);
		return LK_STATUS_OK;
	});
}

lk_room_state_t lk_room_state(const lk_room_t* room) {
	if (room == nullptr || room->room == nullptr) {
		return LK_ROOM_STATE_DISCONNECTED;
	}
	return ToCRoomState(room->room->State());
}

lk_disconnect_reason_t lk_room_disconnect_reason(const lk_room_t* room) {
	if (room == nullptr || room->room == nullptr) {
		return LK_DISCONNECT_REASON_UNKNOWN;
	}
	return ToCDisconnectReason(room->room->LastDisconnectReason());
}

int lk_room_is_connected(const lk_room_t* room) {
	return room != nullptr && room->room != nullptr && room->room->IsConnected() ? 1 : 0;
}

size_t lk_room_sid(const lk_room_t* room, char* buffer, size_t buffer_size) {
	return SizeGuard(
	    [&] { return room != nullptr ? CopyString(room->room->Sid(), buffer, buffer_size) : 0; });
}

size_t lk_room_name(const lk_room_t* room, char* buffer, size_t buffer_size) {
	return SizeGuard(
	    [&] { return room != nullptr ? CopyString(room->room->Name(), buffer, buffer_size) : 0; });
}

size_t lk_room_metadata(const lk_room_t* room, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		return room != nullptr ? CopyString(room->room->Metadata(), buffer, buffer_size) : 0;
	});
}

int lk_room_is_recording(const lk_room_t* room) {
	return room != nullptr && room->room->IsRecording() ? 1 : 0;
}

size_t lk_local_participant_sid(const lk_room_t* room, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		auto* participant = LocalParticipant(room);
		return participant != nullptr ? CopyString(participant->Sid(), buffer, buffer_size) : 0;
	});
}

size_t lk_local_participant_identity(const lk_room_t* room, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		auto* participant = LocalParticipant(room);
		return participant != nullptr ? CopyString(participant->Identity(), buffer, buffer_size)
		                              : 0;
	});
}

size_t lk_local_participant_name(const lk_room_t* room, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		auto* participant = LocalParticipant(room);
		return participant != nullptr ? CopyString(participant->Name(), buffer, buffer_size) : 0;
	});
}

size_t lk_local_participant_metadata(const lk_room_t* room, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		auto* participant = LocalParticipant(room);
		return participant != nullptr ? CopyString(participant->Metadata(), buffer, buffer_size)
		                              : 0;
	});
}

lk_status_t lk_local_participant_set_metadata(lk_room_t* room, const char* metadata) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || metadata == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room and metadata are required");
		}
		return participant->SetMetadata(metadata)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to update metadata");
	});
}

lk_status_t lk_local_participant_set_name(lk_room_t* room, const char* name) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || name == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room and name are required");
		}
		return participant->SetName(name)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to update name");
	});
}

lk_status_t lk_local_participant_set_attributes(lk_room_t* room, const lk_attribute_t* attributes,
                                                size_t attribute_count) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || (attributes == nullptr && attribute_count != 0)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid room or attributes");
		}
		std::map<std::string, std::string> values;
		for (size_t index = 0; index < attribute_count; ++index) {
			if (attributes[index].key == nullptr || attributes[index].value == nullptr) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "attribute key or value is null");
			}
			values[attributes[index].key] = attributes[index].value;
		}
		return participant->SetAttributes(values)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to update attributes");
	});
}

lk_status_t lk_audio_source_create(const lk_audio_source_options_t* options,
                                   lk_audio_source_t** source) {
	return Guard([&] {
		if (source == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "source output is null");
		}
		*source = nullptr;
		lk_audio_source_options_t values;
		lk_audio_source_options_init(&values);
		if (options != nullptr) {
			if (options->struct_size < sizeof(options->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid audio options struct size");
			}
			if (LKC_HAS_FIELD(options, lk_audio_source_options_t, sample_rate)) {
				values.sample_rate = options->sample_rate;
			}
			if (LKC_HAS_FIELD(options, lk_audio_source_options_t, num_channels)) {
				values.num_channels = options->num_channels;
			}
			if (LKC_HAS_FIELD(options, lk_audio_source_options_t, queue_size_ms)) {
				values.queue_size_ms = options->queue_size_ms;
			}
			if (LKC_HAS_FIELD(options, lk_audio_source_options_t, echo_cancellation)) {
				values.echo_cancellation = options->echo_cancellation;
			}
			if (LKC_HAS_FIELD(options, lk_audio_source_options_t, auto_gain_control)) {
				values.auto_gain_control = options->auto_gain_control;
			}
			if (LKC_HAS_FIELD(options, lk_audio_source_options_t, noise_suppression)) {
				values.noise_suppression = options->noise_suppression;
			}
		}
		if (values.sample_rate == 0 || values.num_channels == 0 || values.queue_size_ms == 0) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid audio source dimensions");
		}
		core::AudioSourceOptions core_options;
		core_options.echo_cancellation = values.echo_cancellation != 0;
		core_options.auto_gain_control = values.auto_gain_control != 0;
		core_options.noise_suppression = values.noise_suppression != 0;
		auto result = std::make_unique<lk_audio_source_t>();
		result->source.reset(core::CreateAudioSource(core_options, values.sample_rate,
		                                             values.num_channels, values.queue_size_ms));
		if (!result->source) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to create audio source");
		}
		result->sample_rate = values.sample_rate;
		result->num_channels = values.num_channels;
		*source = result.release();
		return LK_STATUS_OK;
	});
}

lk_status_t lk_audio_source_destroy(lk_audio_source_t* source) {
	if (source == nullptr) {
		return LK_STATUS_OK;
	}
	if (source->track_references.load() != 0) {
		return Failure(LK_STATUS_INVALID_STATE, "audio source is still used by a track");
	}
	delete source;
	return LK_STATUS_OK;
}

lk_status_t lk_audio_source_capture_frame(lk_audio_source_t* source, const int16_t* data,
                                          uint32_t samples_per_channel) {
	return Guard([&] {
		if (source == nullptr || data == nullptr || samples_per_channel == 0) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid audio frame");
		}
		return source->source->CaptureFrame(const_cast<int16_t*>(data), source->sample_rate,
		                                    source->num_channels, samples_per_channel)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to capture audio frame");
	});
}

lk_status_t lk_video_source_create(const lk_video_source_options_t* options,
                                   lk_video_source_t** source) {
	return Guard([&] {
		if (source == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "source output is null");
		}
		*source = nullptr;
		core::VideoSourceOptions core_options;
		if (options != nullptr) {
			if (options->struct_size < sizeof(options->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid video options struct size");
			}
			if (LKC_HAS_FIELD(options, lk_video_source_options_t, is_screencast)) {
				core_options.is_screencast = options->is_screencast != 0;
			}
		}
		auto result = std::make_unique<lk_video_source_t>();
		result->source.reset(core::CreateVideoSource(core_options));
		if (!result->source) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to create video source");
		}
		*source = result.release();
		return LK_STATUS_OK;
	});
}

lk_status_t lk_video_source_destroy(lk_video_source_t* source) {
	if (source == nullptr) {
		return LK_STATUS_OK;
	}
	if (source->track_references.load() != 0) {
		return Failure(LK_STATUS_INVALID_STATE, "video source is still used by a track");
	}
	delete source;
	return LK_STATUS_OK;
}

lk_status_t lk_video_source_capture_i420(lk_video_source_t* source, const uint8_t* data,
                                         size_t data_size, uint32_t width, uint32_t height,
                                         int64_t timestamp_us) {
	return Guard([&] {
		if (source == nullptr || data == nullptr || data_size == 0 || width == 0 || height == 0) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid video frame");
		}
		core::VideoFrame frame;
		frame.data.assign(data, data + data_size);
		frame.width = width;
		frame.height = height;
		frame.timestamp_us = timestamp_us;
		return source->source->CaptureFrame(frame)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to capture video frame");
	});
}

lk_status_t lk_room_create_audio_track(lk_room_t* room, const char* label,
                                       lk_audio_source_t* source, lk_local_track_t** track) {
	return Guard([&] {
		if (track == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "track output is null");
		}
		*track = nullptr;
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || label == nullptr || source == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room, label, and source are required");
		}
		auto result = std::make_unique<lk_local_track_t>();
		result->track.reset(participant->CreateLocalAudioTrack(label, source->source.get()));
		if (!result->track) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to create audio track");
		}
		result->owner = room;
		result->room_state = room->state;
		result->audio_source = source;
		source->track_references.fetch_add(1);
		{
			std::lock_guard<std::mutex> guard(room->local_tracks_mutex);
			room->local_tracks.push_back(result.get());
		}
		*track = result.release();
		return LK_STATUS_OK;
	});
}

lk_status_t lk_room_create_video_track(lk_room_t* room, const char* label,
                                       lk_video_source_t* source, lk_local_track_t** track) {
	return Guard([&] {
		if (track == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "track output is null");
		}
		*track = nullptr;
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || label == nullptr || source == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room, label, and source are required");
		}
		auto result = std::make_unique<lk_local_track_t>();
		result->track.reset(participant->CreateLocalVideoTrack(label, source->source.get()));
		if (!result->track) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to create video track");
		}
		result->owner = room;
		result->room_state = room->state;
		result->video_source = source;
		source->track_references.fetch_add(1);
		{
			std::lock_guard<std::mutex> guard(room->local_tracks_mutex);
			room->local_tracks.push_back(result.get());
		}
		*track = result.release();
		return LK_STATUS_OK;
	});
}

lk_status_t lk_local_track_publish(lk_room_t* room, lk_local_track_t* track,
                                   const lk_track_publish_options_t* options) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || track == nullptr || track->track == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room and track are required");
		}
		if (track->owner != room) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "track belongs to a different room");
		}
		if (track->published.load()) {
			return Failure(LK_STATUS_INVALID_STATE, "track is already published");
		}
		core::TrackPublishOptions publish_options;
		if (options != nullptr) {
			if (options->struct_size < sizeof(options->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid publish options struct size");
			}
			if (LKC_HAS_FIELD(options, lk_track_publish_options_t, source)) {
				publish_options.source = ToCoreTrackSource(options->source);
			}
			if (LKC_HAS_FIELD(options, lk_track_publish_options_t, dtx)) {
				publish_options.dtx = options->dtx != 0;
			}
			if (LKC_HAS_FIELD(options, lk_track_publish_options_t, red)) {
				publish_options.red = options->red != 0;
			}
			if (LKC_HAS_FIELD(options, lk_track_publish_options_t, simulcast)) {
				publish_options.simulcast = options->simulcast != 0;
			}
			if (LKC_HAS_FIELD(options, lk_track_publish_options_t, stream) &&
			    options->stream != nullptr) {
				publish_options.stream = options->stream;
			}
		}
		if (!participant->PublishTrack(track->track.get(), publish_options)) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to publish track");
		}
		track->published.store(true);
		return LK_STATUS_OK;
	});
}

lk_status_t lk_local_track_unpublish(lk_local_track_t* track, int stop_on_unpublish) {
	return Guard([&] {
		if (track == nullptr || track->track == nullptr || track->owner == nullptr ||
		    track->room_state == nullptr || !track->room_state->alive.load()) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "live local track is required");
		}
		if (!track->published.load()) {
			return Failure(LK_STATUS_INVALID_STATE, "track is not published");
		}
		auto* participant = LocalParticipant(track->owner);
		if (participant == nullptr ||
		    !participant->UnpublishTrack(track->track.get(), stop_on_unpublish != 0)) {
			return Failure(LK_STATUS_OPERATION_FAILED, "failed to unpublish track");
		}
		track->published.store(false);
		return LK_STATUS_OK;
	});
}

lk_status_t lk_room_republish_all_tracks(lk_room_t* room) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "live room is required");
		}
		return participant->RepublishAllTracks()
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to republish all tracks");
	});
}

lk_status_t lk_local_track_set_muted(lk_local_track_t* track, int muted) {
	return Guard([&] {
		if (track == nullptr || track->track == nullptr || track->owner == nullptr ||
		    track->room_state == nullptr || !track->room_state->alive.load()) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "live local track is required");
		}
		if (!track->published.load()) {
			return Failure(LK_STATUS_INVALID_STATE, "track is not published");
		}
		return track->owner->room->SetLocalTrackMuted(track->track->Sid(), muted != 0)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to update track mute state");
	});
}

lk_status_t lk_local_track_destroy(lk_local_track_t* track) {
	if (track == nullptr) {
		return LK_STATUS_OK;
	}
	if (track->published.load() && track->room_state != nullptr &&
	    track->room_state->alive.load() && track->room_state->connected.load()) {
		return Failure(LK_STATUS_INVALID_STATE,
		               "disconnect the room before destroying a published track");
	}
	if (track->owner != nullptr && track->room_state != nullptr &&
	    track->room_state->alive.load()) {
		std::lock_guard<std::mutex> guard(track->owner->local_tracks_mutex);
		auto& tracks = track->owner->local_tracks;
		tracks.erase(std::remove(tracks.begin(), tracks.end(), track), tracks.end());
	}
	if (track->audio_source != nullptr) {
		track->audio_source->track_references.fetch_sub(1);
	}
	if (track->video_source != nullptr) {
		track->video_source->track_references.fetch_sub(1);
	}
	delete track;
	return LK_STATUS_OK;
}

lk_status_t lk_room_set_remote_track_subscribed(lk_room_t* room, const char* participant_sid,
                                                const char* track_sid, int subscribed) {
	return Guard([&] {
		if (room == nullptr || participant_sid == nullptr || track_sid == nullptr ||
		    *participant_sid == '\0' || *track_sid == '\0') {
			return Failure(LK_STATUS_INVALID_ARGUMENT,
			               "room, participant SID, and track SID are required");
		}
		return room->room->SetRemoteTrackSubscribed(participant_sid, track_sid, subscribed != 0)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to update track subscription");
	});
}

lk_status_t
lk_room_set_track_subscription_permissions(lk_room_t* room, int all_participants_allowed,
                                           const lk_participant_track_permission_t* permissions,
                                           size_t permission_count) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || (permission_count != 0 && permissions == nullptr)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid room or permission array");
		}
		std::vector<core::ParticipantTrackPermission> converted;
		converted.reserve(permission_count);
		for (size_t index = 0; index < permission_count; ++index) {
			const auto& permission = permissions[index];
			if (permission.struct_size < sizeof(permission.struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid permission struct size");
			}
			core::ParticipantTrackPermission value;
			if (LKC_HAS_FIELD(&permission, lk_participant_track_permission_t, participant_sid) &&
			    permission.participant_sid != nullptr) {
				value.participant_sid = permission.participant_sid;
			}
			if (LKC_HAS_FIELD(&permission, lk_participant_track_permission_t,
			                  participant_identity) &&
			    permission.participant_identity != nullptr) {
				value.participant_identity = permission.participant_identity;
			}
			if (value.participant_sid.empty() && value.participant_identity.empty()) {
				return Failure(LK_STATUS_INVALID_ARGUMENT,
				               "permission participant SID or identity is required");
			}
			if (LKC_HAS_FIELD(&permission, lk_participant_track_permission_t, allow_all)) {
				value.allow_all = permission.allow_all != 0;
			}
			if (LKC_HAS_FIELD(&permission, lk_participant_track_permission_t,
			                  allowed_track_sid_count)) {
				if (permission.allowed_track_sid_count != 0 &&
				    permission.allowed_track_sids == nullptr) {
					return Failure(LK_STATUS_INVALID_ARGUMENT, "allowed track SIDs are null");
				}
				value.allowed_track_sids = DestinationIdentities(
				    permission.allowed_track_sids, permission.allowed_track_sid_count);
				if (value.allowed_track_sids.size() != permission.allowed_track_sid_count) {
					return Failure(LK_STATUS_INVALID_ARGUMENT, "allowed track SID is null");
				}
			}
			converted.push_back(std::move(value));
		}
		return participant->SetTrackSubscriptionPermissions(all_participants_allowed != 0,
		                                                    converted)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED,
		                     "failed to update track subscription permissions");
	});
}

lk_status_t lk_room_publish_data(lk_room_t* room, const uint8_t* data, size_t data_size,
                                 const lk_data_publish_options_t* options) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || (data == nullptr && data_size != 0)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid room or data buffer");
		}
		core::DataPublishOptions publish_options;
		if (options != nullptr) {
			if (options->struct_size < sizeof(options->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid data options struct size");
			}
			if (LKC_HAS_FIELD(options, lk_data_publish_options_t, reliable)) {
				publish_options.reliable = options->reliable != 0;
			}
			if (LKC_HAS_FIELD(options, lk_data_publish_options_t, topic) &&
			    options->topic != nullptr) {
				publish_options.topic = options->topic;
			}
			if (LKC_HAS_FIELD(options, lk_data_publish_options_t, destination_identity_count)) {
				if (options->destination_identity_count != 0 &&
				    options->destination_identities == nullptr) {
					return Failure(LK_STATUS_INVALID_ARGUMENT, "destination identities are null");
				}
				publish_options.destination_identities = DestinationIdentities(
				    options->destination_identities, options->destination_identity_count);
			}
		}
		std::vector<uint8_t> payload;
		if (data_size != 0) {
			payload.assign(data, data + data_size);
		}
		return participant->PublishData(payload, std::move(publish_options))
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to publish data");
	});
}

lk_status_t lk_room_send_text(lk_room_t* room, const char* text,
                              const lk_text_send_options_t* options) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || text == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room and text are required");
		}
		core::TextSendOptions send_options;
		if (options != nullptr) {
			if (options->struct_size < sizeof(options->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid text options struct size");
			}
			if (LKC_HAS_FIELD(options, lk_text_send_options_t, topic) &&
			    options->topic != nullptr) {
				send_options.topic = options->topic;
			}
			if (LKC_HAS_FIELD(options, lk_text_send_options_t, destination_identity_count)) {
				if (options->destination_identity_count != 0 &&
				    options->destination_identities == nullptr) {
					return Failure(LK_STATUS_INVALID_ARGUMENT, "destination identities are null");
				}
				send_options.destination_identities = DestinationIdentities(
				    options->destination_identities, options->destination_identity_count);
			}
			if (LKC_HAS_FIELD(options, lk_text_send_options_t, attribute_count) &&
			    !CopyAttributes(options->attributes, options->attribute_count,
			                    send_options.attributes)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid text attributes");
			}
			if (LKC_HAS_FIELD(options, lk_text_send_options_t, reply_to_stream_id) &&
			    options->reply_to_stream_id != nullptr) {
				send_options.reply_to_stream_id = options->reply_to_stream_id;
			}
			if (LKC_HAS_FIELD(options, lk_text_send_options_t, attached_stream_id_count)) {
				if (options->attached_stream_id_count != 0 &&
				    options->attached_stream_ids == nullptr) {
					return Failure(LK_STATUS_INVALID_ARGUMENT, "attached stream IDs are null");
				}
				send_options.attached_stream_ids = DestinationIdentities(
				    options->attached_stream_ids, options->attached_stream_id_count);
			}
			if (LKC_HAS_FIELD(options, lk_text_send_options_t, chunk_size)) {
				send_options.chunk_size = options->chunk_size;
			}
		}
		return participant->SendText(text, std::move(send_options))
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to send text stream");
	});
}

lk_status_t lk_room_send_bytes(lk_room_t* room, const uint8_t* data, size_t data_size,
                               const lk_byte_send_options_t* options) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || (data == nullptr && data_size != 0)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid room or byte buffer");
		}
		core::ByteSendOptions send_options;
		if (options != nullptr) {
			if (options->struct_size < sizeof(options->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid byte options struct size");
			}
			if (LKC_HAS_FIELD(options, lk_byte_send_options_t, topic) &&
			    options->topic != nullptr) {
				send_options.topic = options->topic;
			}
			if (LKC_HAS_FIELD(options, lk_byte_send_options_t, mime_type) &&
			    options->mime_type != nullptr) {
				send_options.mime_type = options->mime_type;
			}
			if (LKC_HAS_FIELD(options, lk_byte_send_options_t, name) && options->name != nullptr) {
				send_options.name = options->name;
			}
			if (LKC_HAS_FIELD(options, lk_byte_send_options_t, destination_identity_count)) {
				if (options->destination_identity_count != 0 &&
				    options->destination_identities == nullptr) {
					return Failure(LK_STATUS_INVALID_ARGUMENT, "destination identities are null");
				}
				send_options.destination_identities = DestinationIdentities(
				    options->destination_identities, options->destination_identity_count);
			}
			if (LKC_HAS_FIELD(options, lk_byte_send_options_t, attribute_count) &&
			    !CopyAttributes(options->attributes, options->attribute_count,
			                    send_options.attributes)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid byte attributes");
			}
			if (LKC_HAS_FIELD(options, lk_byte_send_options_t, chunk_size)) {
				send_options.chunk_size = options->chunk_size;
			}
		}
		std::vector<uint8_t> payload;
		if (data_size != 0) {
			payload.assign(data, data + data_size);
		}
		return participant->SendBytes(payload, std::move(send_options))
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to send byte stream");
	});
}

lk_status_t lk_room_send_file(lk_room_t* room, const char* path,
                              const lk_file_send_options_t* options) {
	return Guard([&] {
		auto* participant = LocalParticipant(room);
		if (participant == nullptr || path == nullptr || *path == '\0') {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room and file path are required");
		}
		core::FileSendOptions send_options;
		if (options != nullptr) {
			if (options->struct_size < sizeof(options->struct_size)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid file options struct size");
			}
			if (LKC_HAS_FIELD(options, lk_file_send_options_t, topic) &&
			    options->topic != nullptr) {
				send_options.topic = options->topic;
			}
			if (LKC_HAS_FIELD(options, lk_file_send_options_t, mime_type) &&
			    options->mime_type != nullptr) {
				send_options.mime_type = options->mime_type;
			}
			if (LKC_HAS_FIELD(options, lk_file_send_options_t, destination_identity_count)) {
				if (options->destination_identity_count != 0 &&
				    options->destination_identities == nullptr) {
					return Failure(LK_STATUS_INVALID_ARGUMENT, "destination identities are null");
				}
				send_options.destination_identities = DestinationIdentities(
				    options->destination_identities, options->destination_identity_count);
			}
			if (LKC_HAS_FIELD(options, lk_file_send_options_t, chunk_size)) {
				send_options.chunk_size = options->chunk_size;
			}
			if (LKC_HAS_FIELD(options, lk_file_send_options_t, attribute_count) &&
			    !CopyAttributes(options->attributes, options->attribute_count,
			                    send_options.attributes)) {
				return Failure(LK_STATUS_INVALID_ARGUMENT, "invalid file attributes");
			}
		}
		return participant->SendFile(path, std::move(send_options))
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_OPERATION_FAILED, "failed to send file");
	});
}

lk_status_t lk_room_register_rpc_method(lk_room_t* room, const char* method, lk_rpc_handler handler,
                                        void* user_data) {
	return Guard([&] {
		if (room == nullptr || method == nullptr || *method == '\0' || handler == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room, method, and handler are required");
		}
		const bool registered = room->room->RegisterRpcMethod(
		    method, [handler, user_data](const core::RpcInvocationData& invocation) {
			    const lk_rpc_invocation_t c_invocation{
			        invocation.request_id.c_str(), invocation.caller_identity.c_str(),
			        invocation.payload.c_str(),
			        static_cast<uint32_t>(invocation.response_timeout.count())};
			    const auto response = handler(user_data, &c_invocation);
			    if (response.error_code == 0) {
				    return core::RpcResult::Success(response.payload != nullptr ? response.payload
				                                                                : "");
			    }
			    auto error =
			        core::RpcError::BuiltIn(static_cast<core::RpcErrorCode>(response.error_code));
			    if (response.error_message != nullptr) {
				    error.message = response.error_message;
			    }
			    if (response.error_data != nullptr) {
				    error.data = response.error_data;
			    }
			    return core::RpcResult::Failure(std::move(error));
		    });
		return registered ? LK_STATUS_OK
		                  : Failure(LK_STATUS_INVALID_STATE, "RPC method is already registered");
	});
}

lk_status_t lk_room_unregister_rpc_method(lk_room_t* room, const char* method) {
	return Guard([&] {
		if (room == nullptr || method == nullptr || *method == '\0') {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room and method are required");
		}
		return room->room->UnregisterRpcMethod(method)
		           ? LK_STATUS_OK
		           : Failure(LK_STATUS_INVALID_STATE, "RPC method is not registered");
	});
}

lk_status_t lk_room_perform_rpc(lk_room_t* room, const lk_rpc_perform_options_t* options,
                                lk_rpc_result_t** result) {
	return Guard([&] {
		if (result == nullptr) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "RPC result output is null");
		}
		*result = nullptr;
		if (room == nullptr || options == nullptr ||
		    options->struct_size < sizeof(options->struct_size)) {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "room and valid RPC options are required");
		}
		if (!LKC_HAS_FIELD(options, lk_rpc_perform_options_t, destination_identity) ||
		    options->destination_identity == nullptr || *options->destination_identity == '\0' ||
		    !LKC_HAS_FIELD(options, lk_rpc_perform_options_t, method) ||
		    options->method == nullptr || *options->method == '\0') {
			return Failure(LK_STATUS_INVALID_ARGUMENT, "RPC destination and method are required");
		}

		core::PerformRpcParams params;
		params.destination_identity = options->destination_identity;
		params.method = options->method;
		if (LKC_HAS_FIELD(options, lk_rpc_perform_options_t, payload) &&
		    options->payload != nullptr) {
			params.payload = options->payload;
		}
		if (LKC_HAS_FIELD(options, lk_rpc_perform_options_t, response_timeout_ms) &&
		    options->response_timeout_ms != 0) {
			params.response_timeout = std::chrono::milliseconds(options->response_timeout_ms);
		}
		auto* participant = LocalParticipant(room);
		if (participant == nullptr) {
			return Failure(LK_STATUS_INVALID_STATE, "local participant is unavailable");
		}
		auto owned = std::make_unique<lk_rpc_result_t>();
		owned->result = participant->PerformRpc(params);
		*result = owned.release();
		return LK_STATUS_OK;
	});
}

void lk_rpc_result_destroy(lk_rpc_result_t* result) { delete result; }

int lk_rpc_result_ok(const lk_rpc_result_t* result) {
	return result != nullptr && result->result.Ok() ? 1 : 0;
}

size_t lk_rpc_result_payload(const lk_rpc_result_t* result, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		return result != nullptr ? CopyString(result->result.payload, buffer, buffer_size) : 0;
	});
}

uint32_t lk_rpc_result_error_code(const lk_rpc_result_t* result) {
	return result != nullptr && result->result.error
	           ? static_cast<uint32_t>(result->result.error->code)
	           : 0;
}

size_t lk_rpc_result_error_message(const lk_rpc_result_t* result, char* buffer,
                                   size_t buffer_size) {
	return SizeGuard([&] {
		return result != nullptr && result->result.error
		           ? CopyString(result->result.error->message, buffer, buffer_size)
		           : 0;
	});
}

size_t lk_rpc_result_error_data(const lk_rpc_result_t* result, char* buffer, size_t buffer_size) {
	return SizeGuard([&] {
		return result != nullptr && result->result.error
		           ? CopyString(result->result.error->data, buffer, buffer_size)
		           : 0;
	});
}

} // extern "C"
