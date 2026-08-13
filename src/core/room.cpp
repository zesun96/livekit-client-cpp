/**
 *
 * Copyright (c) 2024 sunze
 *
 *Licensed under the Apache License, Version 2.0 (the "License");
 *you may not use this file except in compliance with the License.
 *You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *Unless required by applicable law or agreed to in writing, software
 *distributed under the License is distributed on an "AS IS" BASIS,
 *WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *See the License for the specific language governing permissions and
 *limitations under the License.
 */

#include "room.h"
#include "detail/converted_proto.h"
#include "detail/rtc_engine.h"
#include "track/audio_track.h"
#include "track/remote_audio_track.h"
#include "track/remote_video_track.h"
#include "track/track_publication.h"
#include "track/video_track.h"

#include <algorithm>
#include <limits>
#include <tuple>

namespace {
constexpr uint64_t kMaximumBufferedDataStreamSize = 64ULL * 1024 * 1024;

static livekit::core::EngineOptions make_engine_config(livekit::core::RoomOptions room_options) {
	livekit::core::EngineOptions engine_options;

	engine_options.join_retries = room_options.join_retries;
	engine_options.rtc_config.ice_servers = room_options.rtc_config.ice_servers;
	engine_options.rtc_config.continual_gathering_policy =
	    room_options.rtc_config.continual_gathering_policy;
	engine_options.rtc_config.ice_transport_type = room_options.rtc_config.ice_transport_type;
	engine_options.signal_options.reconnect = false;
	engine_options.signal_options.adaptive_stream = room_options.adaptive_stream;
	engine_options.signal_options.auto_subscribe = room_options.auto_subscribe;
	engine_options.signal_options.sdk_options.sdk = room_options.sdk_options.sdk;
	engine_options.signal_options.sdk_options.sdk_version = room_options.sdk_options.sdk_version;
	return engine_options;
}

static livekit::core::ConnectionQuality
from_connection_quality(livekit::ConnectionQuality quality) {
	switch (quality) {
	case livekit::ConnectionQuality::POOR:
		return livekit::core::ConnectionQuality::Poor;
	case livekit::ConnectionQuality::GOOD:
		return livekit::core::ConnectionQuality::Good;
	case livekit::ConnectionQuality::EXCELLENT:
		return livekit::core::ConnectionQuality::Excellent;
	case livekit::ConnectionQuality::LOST:
		return livekit::core::ConnectionQuality::Lost;
	default:
		return livekit::core::ConnectionQuality::Unknown;
	}
}
} // namespace

namespace livekit {
namespace core {
Room::Room(RoomOptions options) : options_(options) {
	rtc_engine_ = std::make_unique<RtcEngine>();
	rtc_engine_->SetRoomObserver(this);
	local_participant_ = std::make_unique<LocalParticipant>("", "", EncryptionType::None,
	                                                        rtc_engine_.get(), options_);
}

Room::~Room() {
	std::map<std::string, std::shared_ptr<RemoteTrack>> detached_tracks;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		detached_tracks.swap(remote_tracks_);
		pending_media_tracks_.clear();
	}
	detached_tracks.clear();
	if (rtc_engine_) {
		rtc_engine_->SetRoomObserver(nullptr);
		rtc_engine_->Disconnect();
	}
}

bool Room::Connect(std::string url, std::string token, RoomConnectOptions opts) {
	auto expected = state_.load();
	if (expected != RoomState::Disconnected && expected != RoomState::Failed) {
		return false;
	}
	if (!state_.compare_exchange_strong(expected, RoomState::Connecting)) {
		return false;
	}

	try {
		EngineOptions engine_options = make_engine_config(opts);
		livekit::JoinResponse join_response = rtc_engine_->Connect(url, token, engine_options);
		if (!join_response.has_room()) {
			rtc_engine_->Disconnect();
			state_ = RoomState::Failed;
			return false;
		}
		if (join_response.has_server_info()) {
			server_info_ = from_proto(join_response.server_info());
		} else {
			server_info_.region = join_response.server_region();
			server_info_.version = join_response.server_version();
		}

		std::vector<livekit::ParticipantInfo> participants;
		participants.reserve(static_cast<std::size_t>(join_response.other_participants_size()));
		if (join_response.has_participant()) {
			local_participant_->UpdateFromInfo(join_response.participant());
		}
		for (const auto& participant : join_response.other_participants()) {
			participants.push_back(participant);
		}
		{
			std::lock_guard<std::mutex> guard(room_info_mutex_);
			room_info_ = join_response.room();
		}
		ApplyParticipantUpdates(participants, false);
	} catch (...) {
		state_ = RoomState::Failed;
		throw;
	}

	return true;
}

void Room::AddEventListener(RoomEventInterface* listener) {
	event_listener_.store(listener);
	local_participant_->SetEventListener(listener);
}

void Room::RemoveEventListener() {
	local_participant_->SetEventListener(nullptr);
	event_listener_.store(nullptr);
}

bool Room::IsConnected() { return state_.load() == RoomState::Connected; }

RoomInterface::RoomState Room::State() const { return state_.load(); }

std::string Room::Sid() {
	std::lock_guard<std::mutex> guard(room_info_mutex_);
	return room_info_.sid();
}

std::string Room::Name() {
	std::lock_guard<std::mutex> guard(room_info_mutex_);
	return room_info_.name();
}

std::string Room::Metadata() {
	std::lock_guard<std::mutex> guard(room_info_mutex_);
	return room_info_.metadata();
}

bool Room::IsRecording() {
	std::lock_guard<std::mutex> guard(room_info_mutex_);
	return room_info_.active_recording();
}

bool Room::Disconnect() {
	auto state = state_.load();
	if (state == RoomState::Disconnecting || state == RoomState::Disconnected) {
		return false;
	}
	state_ = RoomState::Disconnecting;

	std::map<std::string, std::shared_ptr<RemoteTrack>> detached_tracks;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		detached_tracks.swap(remote_tracks_);
		pending_media_tracks_.clear();
		remote_participants_.clear();
	}
	detached_tracks.clear();
	rtc_engine_->Disconnect();
	{
		std::lock_guard<std::mutex> guard(incoming_streams_mutex_);
		incoming_files_.clear();
		incoming_texts_.clear();
	}
	state_ = RoomState::Disconnected;
	if (auto* listener = event_listener_.load()) {
		listener->OnDisconnected();
	}
	return true;
}

LocalParticipantInterface* Room::GetLocalParticipant() { return this->local_participant_.get(); }

std::vector<RemoteParticipantInterface*> Room::GetRemoteParticipants() {
	std::lock_guard<std::mutex> guard(participants_mutex_);
	std::vector<RemoteParticipantInterface*> participants;
	for (auto& participant : this->remote_participants_) {
		participants.push_back(participant.second.get());
	}
	return participants;
}

RemoteParticipantInterface* Room::GetRemoteParticipantBySid(std::string sid) {
	std::lock_guard<std::mutex> guard(participants_mutex_);
	auto participant = remote_participants_.find(sid);
	if (participant != remote_participants_.end()) {
		return participant->second.get();
	}
	return nullptr;
}

RemoteParticipantInterface* Room::GetRemoteParticipantByName(std::string name) {
	std::lock_guard<std::mutex> guard(participants_mutex_);
	for (auto& participant : this->remote_participants_) {
		if (participant.second->Name() == name) {
			return participant.second.get();
		}
	}
	return nullptr;
}

std::vector<ParticipantInterface*> Room::Participants() {
	std::lock_guard<std::mutex> guard(participants_mutex_);
	std::vector<ParticipantInterface*> participants;
	participants.push_back(this->local_participant_.get());
	for (auto& participant : this->remote_participants_) {
		participants.push_back(participant.second.get());
	}
	return participants;
}

ParticipantInterface* Room::GetParticipantBySid(std::string sid) {
	if (this->local_participant_->Sid() == sid) {
		return this->local_participant_.get();
	}
	return GetRemoteParticipantBySid(std::move(sid));
}

ParticipantInterface* Room::GetParticipantByName(std::string name) {
	if (this->local_participant_->Name() == name) {
		return this->local_participant_.get();
	}
	return GetRemoteParticipantByName(std::move(name));
}

bool Room::SetLocalTrackMutedInternal(std::string track_sid, bool muted) {
	if (track_sid.empty()) {
		return false;
	}
	auto publications = local_participant_->TrackPublicationsSnapshot();
	auto found = publications.find(track_sid);
	if (found == publications.end() || !rtc_engine_->SetTrackMuted(track_sid, muted)) {
		return false;
	}
	if (auto* local_track = dynamic_cast<LocalTrack*>(found->second->Track())) {
		if (local_track->media_track() != nullptr) {
			local_track->media_track()->set_enabled(!muted);
		}
	}
	if (auto* publication = dynamic_cast<TrackPublication*>(found->second.get())) {
		publication->SetMuted(muted);
	}
	if (auto* listener = event_listener_.load()) {
		if (muted) {
			listener->OnTrackMuted(found->second.get(), local_participant_.get());
		} else {
			listener->OnTrackUnmuted(found->second.get(), local_participant_.get());
		}
	}
	return true;
}

bool Room::SetRemoteTrackSubscribedInternal(std::string participant_sid, std::string track_sid,
                                            bool subscribed) {
	if (participant_sid.empty() || track_sid.empty()) {
		return false;
	}
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		auto participant = remote_participants_.find(participant_sid);
		if (participant == remote_participants_.end() ||
		    !participant->second->HasTrackSid(track_sid)) {
			return false;
		}
	}
	return rtc_engine_->SetTrackSubscribed(participant_sid, track_sid, subscribed);
}

RoomInterface::RoomState RoomInterface::State() const {
	auto* room = dynamic_cast<const Room*>(this);
	if (room != nullptr) {
		return room->State();
	}
	return const_cast<RoomInterface*>(this)->IsConnected() ? RoomState::Connected
	                                                       : RoomState::Disconnected;
}

RemoteParticipantInterface* RoomInterface::GetRemoteParticipantByIdentity(std::string identity) {
	for (auto* participant : GetRemoteParticipants()) {
		if (participant != nullptr && participant->Identity() == identity) {
			return participant;
		}
	}
	return nullptr;
}

ParticipantInterface* RoomInterface::GetParticipantByIdentity(std::string identity) {
	auto* local = GetLocalParticipant();
	if (local != nullptr && local->Identity() == identity) {
		return local;
	}
	return GetRemoteParticipantByIdentity(std::move(identity));
}

bool RoomInterface::SetLocalTrackMuted(std::string track_sid, bool muted) {
	auto* room = dynamic_cast<Room*>(this);
	return room != nullptr && room->SetLocalTrackMutedInternal(std::move(track_sid), muted);
}

bool RoomInterface::SetRemoteTrackSubscribed(std::string participant_sid, std::string track_sid,
                                             bool subscribed) {
	auto* room = dynamic_cast<Room*>(this);
	return room != nullptr && room->SetRemoteTrackSubscribedInternal(
	                              std::move(participant_sid), std::move(track_sid), subscribed);
}

void Room::ConnectedEvent(livekit::JoinResponse join_resp) {
	state_ = RoomState::Connected;
	if (auto* listener = event_listener_.load()) {
		listener->OnConnected();
	}
}

void Room::ParticipantUpdateEvent(const std::vector<livekit::ParticipantInfo>& updates) {
	ApplyParticipantUpdates(updates);
}

void Room::RemoteMuteChangedEvent(const std::string& sid, bool muted) {
	std::shared_ptr<RemoteParticipant> participant;
	std::shared_ptr<TrackPublicationInterface> publication;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		participant = FindRemoteParticipantForTrack(sid);
		if (!participant) {
			return;
		}
		auto publications = participant->TrackPublicationsSnapshot();
		auto found = publications.find(sid);
		if (found == publications.end()) {
			return;
		}
		publication = found->second;
		if (publication->IsMuted() == muted) {
			return;
		}
		if (auto* concrete = dynamic_cast<TrackPublication*>(publication.get())) {
			concrete->SetMuted(muted);
		}
	}
	if (auto* listener = event_listener_.load()) {
		if (muted) {
			listener->OnTrackMuted(publication.get(), participant.get());
		} else {
			listener->OnTrackUnmuted(publication.get(), participant.get());
		}
	}
}

void Room::LocalTrackUnpublishedEvent(const std::string& sid) {
	auto publications = local_participant_->TrackPublicationsSnapshot();
	auto found = publications.find(sid);
	if (found == publications.end()) {
		return;
	}
	auto publication = found->second;
	if (auto* local_track = dynamic_cast<LocalTrack*>(publication->Track())) {
		if (local_participant_->UnpublishTrack(local_track, true)) {
			return;
		}
		local_track->SetTransceiver(nullptr);
		local_track->SetEnabled(false);
	}
	local_participant_->RemoveTrackPublication(sid);
	if (auto* listener = event_listener_.load()) {
		listener->OnLocalTrackUnpublished(publication.get(), local_participant_.get());
	}
}

void Room::SpeakersChangedEvent(const std::vector<livekit::SpeakerInfo>& updates) {
	std::vector<ParticipantInterface*> active_speakers;
	std::vector<std::shared_ptr<RemoteParticipant>> retained_participants;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		for (const auto& update : updates) {
			Participant* participant = nullptr;
			if (update.sid() == local_participant_->Sid()) {
				participant = local_participant_.get();
			} else {
				auto found = remote_participants_.find(update.sid());
				if (found != remote_participants_.end()) {
					retained_participants.push_back(found->second);
					participant = found->second.get();
				}
			}
			if (participant == nullptr) {
				continue;
			}
			participant->SetSpeakerInfo(update.level(), update.active());
		}
		if (local_participant_->IsSpeaking()) {
			active_speakers.push_back(local_participant_.get());
		}
		retained_participants.clear();
		for (const auto& [sid, participant] : remote_participants_) {
			if (participant->IsSpeaking()) {
				retained_participants.push_back(participant);
				active_speakers.push_back(participant.get());
			}
		}
	}
	std::sort(active_speakers.begin(), active_speakers.end(),
	          [](ParticipantInterface* left, ParticipantInterface* right) {
		          return left->AudioLevel() > right->AudioLevel();
	          });
	if (auto* listener = event_listener_.load()) {
		listener->OnActiveSpeakersChanged(active_speakers);
	}
}

void Room::RoomUpdateEvent(const livekit::Room& update) {
	bool metadata_changed = false;
	{
		std::lock_guard<std::mutex> guard(room_info_mutex_);
		metadata_changed = room_info_.metadata() != update.metadata();
		room_info_ = update;
	}
	if (metadata_changed) {
		if (auto* listener = event_listener_.load()) {
			listener->OnRoomMetadataChanged(update.metadata());
		}
	}
}

void Room::ConnectionQualityEvent(const std::vector<livekit::ConnectionQualityInfo>& updates) {
	struct QualityEvent {
		ConnectionQuality quality;
		ParticipantInterface* participant;
		std::shared_ptr<RemoteParticipant> retained_participant;
	};
	std::vector<QualityEvent> events;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		for (const auto& update : updates) {
			const auto quality = from_connection_quality(update.quality());
			if (update.participant_sid() == local_participant_->Sid()) {
				if (local_participant_->GetConnectionQuality() != quality) {
					local_participant_->SetConnectionQuality(quality);
					events.push_back({quality, local_participant_.get(), nullptr});
				}
				continue;
			}
			auto found = remote_participants_.find(update.participant_sid());
			if (found != remote_participants_.end() &&
			    found->second->GetConnectionQuality() != quality) {
				found->second->SetConnectionQuality(quality);
				events.push_back({quality, found->second.get(), found->second});
			}
		}
	}
	if (auto* listener = event_listener_.load()) {
		for (const auto& event : events) {
			listener->OnConnectionQualityChanged(event.quality, event.participant);
		}
	}
}

void Room::MediaTrackEvent(webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface> rtc_track) {
	if (!rtc_track) {
		return;
	}
	const std::string track_sid = rtc_track->id();
	std::string participant_sid;
	std::shared_ptr<RemoteParticipant> participant;
	std::shared_ptr<RemoteTrack> subscribed_track;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		participant = FindRemoteParticipantForTrack(track_sid);
		if (!participant) {
			pending_media_tracks_[track_sid] = rtc_track;
			return;
		}
		if (remote_tracks_.count(track_sid) != 0) {
			return;
		}
		participant_sid = participant->Sid();
		if (rtc_track->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
			auto media =
			    std::make_unique<AudioTrack>(webrtc::scoped_refptr<webrtc::AudioTrackInterface>(
			        static_cast<webrtc::AudioTrackInterface*>(rtc_track.get())));
			auto remote = std::make_shared<RemoteAudioTrack>(
			    track_sid, track_sid, std::move(media),
			    [this, participant_sid, track_sid](const AudioFrame& frame) {
				    NotifyAudioFrame(participant_sid, track_sid, frame);
			    });
			subscribed_track = std::move(remote);
			remote_tracks_.emplace(track_sid, subscribed_track);
		} else if (rtc_track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
			auto media =
			    std::make_unique<VideoTrack>(webrtc::scoped_refptr<webrtc::VideoTrackInterface>(
			        static_cast<webrtc::VideoTrackInterface*>(rtc_track.get())));
			auto remote = std::make_shared<RemoteVideoTrack>(
			    track_sid, track_sid, std::move(media),
			    [this, participant_sid, track_sid](const VideoFrame& frame) {
				    NotifyVideoFrame(participant_sid, track_sid, frame);
			    });
			subscribed_track = std::move(remote);
			remote_tracks_.emplace(track_sid, subscribed_track);
		}
		if (subscribed_track) {
			for (auto* candidate : participant->GetTrackPublications()) {
				if (candidate->Sid() == track_sid) {
					if (auto* concrete = dynamic_cast<TrackPublication*>(candidate)) {
						concrete->SetTrack(subscribed_track.get());
					}
					break;
				}
			}
		}
	}
	if (subscribed_track) {
		if (auto* listener = event_listener_.load()) {
			listener->OnTrackSubscribed(subscribed_track.get(), participant.get());
		}
	}
}

void Room::DataPacketEvent(const livekit::DataPacket& packet) {
	if (packet.has_user()) {
		DataReceivedEvent event;
		event.payload.assign(packet.user().payload().begin(), packet.user().payload().end());
		event.topic = packet.user().has_topic() ? packet.user().topic() : "";
		event.participant_identity = packet.participant_identity();
		if (event.participant_identity.empty()) {
			event.participant_identity = packet.user().participant_identity();
		}
		event.reliable = packet.kind() != livekit::DataPacket_Kind_LOSSY;
		if (auto* listener = event_listener_.load()) {
			listener->OnDataReceived(event);
		}
		return;
	}
	if (packet.has_stream_header() && packet.stream_header().has_text_header()) {
		const auto& header = packet.stream_header();
		if (header.stream_id().empty() ||
		    header.compression() != livekit::DataStream_CompressionType_NONE) {
			return;
		}
		IncomingText incoming;
		incoming.event.stream_id = header.stream_id();
		incoming.event.topic = header.topic();
		incoming.event.participant_identity = packet.participant_identity();
		incoming.event.reply_to_stream_id = header.text_header().reply_to_stream_id();
		incoming.event.attached_stream_ids.assign(
		    header.text_header().attached_stream_ids().begin(),
		    header.text_header().attached_stream_ids().end());
		incoming.event.attributes.insert(header.attributes().begin(), header.attributes().end());
		incoming.event.timestamp = header.timestamp();
		if (header.has_total_length()) {
			incoming.expected_length = header.total_length();
			if (*incoming.expected_length > std::numeric_limits<std::size_t>::max() ||
			    *incoming.expected_length > kMaximumBufferedDataStreamSize) {
				return;
			}
			try {
				incoming.event.text.reserve(static_cast<std::size_t>(*incoming.expected_length));
			} catch (const std::exception&) {
				return;
			}
		}
		if (header.has_inline_content()) {
			if (incoming.expected_length &&
			    header.inline_content().size() != *incoming.expected_length) {
				return;
			}
			incoming.event.text = header.inline_content();
			if (auto* listener = event_listener_.load()) {
				listener->OnTextReceived(incoming.event);
			}
			return;
		}
		std::lock_guard<std::mutex> guard(incoming_streams_mutex_);
		incoming_files_.erase(header.stream_id());
		incoming_texts_[header.stream_id()] = std::move(incoming);
		return;
	}
	if (packet.has_stream_header() && packet.stream_header().has_byte_header()) {
		const auto& header = packet.stream_header();
		if (header.stream_id().empty() ||
		    header.compression() != livekit::DataStream_CompressionType_NONE) {
			return;
		}
		IncomingFile incoming;
		incoming.event.stream_id = header.stream_id();
		incoming.event.name = header.byte_header().name();
		incoming.event.mime_type = header.mime_type();
		incoming.event.topic = header.topic();
		incoming.event.participant_identity = packet.participant_identity();
		incoming.event.attributes.insert(header.attributes().begin(), header.attributes().end());
		incoming.event.timestamp = header.timestamp();
		if (header.has_total_length()) {
			incoming.expected_length = header.total_length();
			if (*incoming.expected_length > std::numeric_limits<std::size_t>::max() ||
			    *incoming.expected_length > kMaximumBufferedDataStreamSize) {
				return;
			}
			try {
				incoming.event.data.reserve(static_cast<std::size_t>(*incoming.expected_length));
			} catch (const std::exception&) {
				return;
			}
		}
		if (header.has_inline_content()) {
			if (incoming.expected_length &&
			    header.inline_content().size() != *incoming.expected_length) {
				return;
			}
			incoming.event.data.assign(header.inline_content().begin(),
			                           header.inline_content().end());
			if (auto* listener = event_listener_.load()) {
				ByteReceivedEvent byte_event;
				static_cast<FileReceivedEvent&>(byte_event) = incoming.event;
				listener->OnByteReceived(byte_event);
				if (!incoming.event.name.empty()) {
					listener->OnFileReceived(incoming.event);
				}
			}
			return;
		}
		std::lock_guard<std::mutex> guard(incoming_streams_mutex_);
		incoming_texts_.erase(header.stream_id());
		incoming_files_[header.stream_id()] = std::move(incoming);
		return;
	}
	if (packet.has_stream_chunk()) {
		const auto& chunk = packet.stream_chunk();
		std::lock_guard<std::mutex> guard(incoming_streams_mutex_);
		auto text = incoming_texts_.find(chunk.stream_id());
		if (text != incoming_texts_.end()) {
			if (chunk.chunk_index() != text->second.next_chunk) {
				incoming_texts_.erase(text);
				return;
			}
			const auto content_size = static_cast<uint64_t>(chunk.content().size());
			if (content_size > kMaximumBufferedDataStreamSize - text->second.event.text.size() ||
			    (text->second.expected_length &&
			     (text->second.event.text.size() > *text->second.expected_length ||
			      content_size > *text->second.expected_length - text->second.event.text.size()))) {
				incoming_texts_.erase(text);
				return;
			}
			text->second.event.text.append(chunk.content());
			++text->second.next_chunk;
			return;
		}
		auto it = incoming_files_.find(chunk.stream_id());
		if (it == incoming_files_.end()) {
			return;
		}
		if (chunk.chunk_index() != it->second.next_chunk) {
			incoming_files_.erase(it);
			return;
		}
		const auto content_size = static_cast<uint64_t>(chunk.content().size());
		if (content_size > kMaximumBufferedDataStreamSize - it->second.event.data.size() ||
		    (it->second.expected_length &&
		     (it->second.event.data.size() > *it->second.expected_length ||
		      content_size > *it->second.expected_length - it->second.event.data.size()))) {
			incoming_files_.erase(it);
			return;
		}
		it->second.event.data.insert(it->second.event.data.end(), chunk.content().begin(),
		                             chunk.content().end());
		++it->second.next_chunk;
		return;
	}
	if (packet.has_stream_trailer()) {
		TextReceivedEvent text_event;
		bool has_text_event = false;
		FileReceivedEvent event;
		bool has_byte_event = false;
		{
			std::lock_guard<std::mutex> guard(incoming_streams_mutex_);
			auto text = incoming_texts_.find(packet.stream_trailer().stream_id());
			if (text != incoming_texts_.end()) {
				if (packet.stream_trailer().reason().empty() &&
				    (!text->second.expected_length ||
				     text->second.event.text.size() == *text->second.expected_length)) {
					for (const auto& [key, value] : packet.stream_trailer().attributes()) {
						text->second.event.attributes[key] = value;
					}
					text_event = std::move(text->second.event);
					has_text_event = true;
				}
				incoming_texts_.erase(text);
			}
			auto it = incoming_files_.find(packet.stream_trailer().stream_id());
			if (it != incoming_files_.end()) {
				if (packet.stream_trailer().reason().empty() &&
				    (!it->second.expected_length ||
				     it->second.event.data.size() == *it->second.expected_length)) {
					for (const auto& [key, value] : packet.stream_trailer().attributes()) {
						it->second.event.attributes[key] = value;
					}
					event = std::move(it->second.event);
					has_byte_event = true;
				}
				incoming_files_.erase(it);
			}
		}
		if (auto* listener = event_listener_.load()) {
			if (has_text_event) {
				listener->OnTextReceived(text_event);
			}
			if (has_byte_event) {
				ByteReceivedEvent byte_event;
				static_cast<FileReceivedEvent&>(byte_event) = event;
				listener->OnByteReceived(byte_event);
				if (!event.name.empty()) {
					listener->OnFileReceived(event);
				}
			}
		}
	}
}

std::shared_ptr<RemoteParticipant>
Room::FindRemoteParticipantForTrack(const std::string& track_sid) {
	for (auto& [sid, participant] : remote_participants_) {
		if (participant->HasTrackSid(track_sid)) {
			return participant;
		}
	}
	return nullptr;
}

void Room::NotifyAudioFrame(const std::string& participant_sid, const std::string& track_sid,
                            const AudioFrame& frame) {
	std::shared_ptr<RemoteParticipant> participant;
	std::shared_ptr<RemoteTrack> track;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		auto it = remote_participants_.find(participant_sid);
		auto track_it = remote_tracks_.find(track_sid);
		if (it == remote_participants_.end() || track_it == remote_tracks_.end()) {
			return;
		}
		participant = it->second;
		track = track_it->second;
	}
	if (auto* listener = event_listener_.load()) {
		listener->OnAudioFrame(track.get(), participant.get(), frame);
	}
}

void Room::NotifyVideoFrame(const std::string& participant_sid, const std::string& track_sid,
                            const VideoFrame& frame) {
	std::shared_ptr<RemoteParticipant> participant;
	std::shared_ptr<RemoteTrack> track;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		auto it = remote_participants_.find(participant_sid);
		auto track_it = remote_tracks_.find(track_sid);
		if (it == remote_participants_.end() || track_it == remote_tracks_.end()) {
			return;
		}
		participant = it->second;
		track = track_it->second;
	}
	if (auto* listener = event_listener_.load()) {
		listener->OnVideoFrame(track.get(), participant.get(), frame);
	}
}

void Room::ApplyParticipantUpdates(const std::vector<livekit::ParticipantInfo>& updates,
                                   bool emit_events) {
	struct PublicationEvent {
		std::shared_ptr<TrackPublicationInterface> publication;
		std::shared_ptr<RemoteParticipant> participant;
	};
	struct ParticipantValueEvent {
		std::string value;
		ParticipantInterface* participant;
		std::shared_ptr<RemoteParticipant> retained_participant;
	};
	struct AttributesEvent {
		std::map<std::string, std::string> changes;
		ParticipantInterface* participant;
		std::shared_ptr<RemoteParticipant> retained_participant;
	};
	struct MuteEvent {
		std::shared_ptr<TrackPublicationInterface> publication;
		ParticipantInterface* participant;
		std::shared_ptr<RemoteParticipant> retained_participant;
		bool muted;
	};

	std::vector<webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface>> ready_tracks;
	std::vector<std::shared_ptr<RemoteParticipant>> connected;
	std::vector<std::shared_ptr<RemoteParticipant>> disconnected;
	std::vector<PublicationEvent> published;
	std::vector<PublicationEvent> unpublished;
	std::vector<ParticipantValueEvent> metadata_changed;
	std::vector<ParticipantValueEvent> name_changed;
	std::vector<AttributesEvent> attributes_changed;
	std::vector<MuteEvent> mute_changed;

	auto attribute_changes = [](const std::map<std::string, std::string>& old_attributes,
	                            const std::map<std::string, std::string>& new_attributes) {
		std::map<std::string, std::string> changes;
		for (const auto& [key, value] : new_attributes) {
			auto old = old_attributes.find(key);
			if (old == old_attributes.end() || old->second != value) {
				changes[key] = value;
			}
		}
		for (const auto& [key, value] : old_attributes) {
			if (new_attributes.count(key) == 0) {
				changes[key] = "";
			}
		}
		return changes;
	};

	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		for (const auto& info : updates) {
			if (info.sid().empty()) {
				continue;
			}
			if (info.sid() == local_participant_->Sid() ||
			    (!info.identity().empty() && info.identity() == local_participant_->Identity())) {
				const auto old_metadata = local_participant_->Metadata();
				const auto old_name = local_participant_->Name();
				const auto old_attributes = local_participant_->Attributes();
				const auto old_publications = local_participant_->TrackPublicationsSnapshot();
				std::map<std::string, bool> old_mutes;
				for (const auto& [sid, publication] : old_publications) {
					old_mutes[sid] = publication->IsMuted();
				}
				local_participant_->UpdateFromInfo(info);
				if (emit_events && old_metadata != local_participant_->Metadata()) {
					metadata_changed.push_back({old_metadata, local_participant_.get(), nullptr});
				}
				if (emit_events && old_name != local_participant_->Name()) {
					name_changed.push_back(
					    {local_participant_->Name(), local_participant_.get(), nullptr});
				}
				if (emit_events) {
					auto changes =
					    attribute_changes(old_attributes, local_participant_->Attributes());
					if (!changes.empty()) {
						attributes_changed.push_back(
						    {std::move(changes), local_participant_.get(), nullptr});
					}
					auto new_publications = local_participant_->TrackPublicationsSnapshot();
					for (const auto& [sid, publication] : new_publications) {
						auto old = old_mutes.find(sid);
						if (old != old_mutes.end() && old->second != publication->IsMuted()) {
							mute_changed.push_back({publication, local_participant_.get(), nullptr,
							                        publication->IsMuted()});
						}
					}
				}
				continue;
			}

			auto participant = remote_participants_.find(info.sid());
			if (info.state() == livekit::ParticipantInfo_State_DISCONNECTED) {
				if (participant != remote_participants_.end()) {
					auto retained = participant->second;
					for (const auto& [sid, publication] : retained->TrackPublicationsSnapshot()) {
						if (auto* concrete = dynamic_cast<TrackPublication*>(publication.get())) {
							concrete->SetTrack(nullptr);
						}
						remote_tracks_.erase(sid);
						pending_media_tracks_.erase(sid);
						if (emit_events) {
							unpublished.push_back({publication, retained});
						}
					}
					remote_participants_.erase(participant);
					if (emit_events) {
						disconnected.push_back(std::move(retained));
					}
				}
				continue;
			}

			if (participant == remote_participants_.end()) {
				auto added = std::make_shared<RemoteParticipant>(info);
				remote_participants_.emplace(info.sid(), added);
				if (emit_events) {
					connected.push_back(added);
					for (const auto& [sid, publication] : added->TrackPublicationsSnapshot()) {
						published.push_back({publication, added});
					}
				}
				continue;
			}

			auto retained = participant->second;
			const auto old_metadata = retained->Metadata();
			const auto old_name = retained->Name();
			const auto old_attributes = retained->Attributes();
			const auto old_publications = retained->TrackPublicationsSnapshot();
			std::map<std::string, bool> old_mutes;
			for (const auto& [sid, publication] : old_publications) {
				old_mutes[sid] = publication->IsMuted();
			}
			retained->UpdateFromInfo(info);
			const auto new_publications = retained->TrackPublicationsSnapshot();
			if (!emit_events) {
				continue;
			}
			if (old_metadata != retained->Metadata()) {
				metadata_changed.push_back({old_metadata, retained.get(), retained});
			}
			if (old_name != retained->Name()) {
				name_changed.push_back({retained->Name(), retained.get(), retained});
			}
			auto changes = attribute_changes(old_attributes, retained->Attributes());
			if (!changes.empty()) {
				attributes_changed.push_back({std::move(changes), retained.get(), retained});
			}
			for (const auto& [sid, publication] : new_publications) {
				auto old = old_publications.find(sid);
				if (old == old_publications.end()) {
					published.push_back({publication, retained});
				} else if (old_mutes[sid] != publication->IsMuted()) {
					mute_changed.push_back(
					    {publication, retained.get(), retained, publication->IsMuted()});
				}
			}
			for (const auto& [sid, publication] : old_publications) {
				if (new_publications.count(sid) == 0) {
					if (auto* concrete = dynamic_cast<TrackPublication*>(publication.get())) {
						concrete->SetTrack(nullptr);
					}
					remote_tracks_.erase(sid);
					pending_media_tracks_.erase(sid);
					unpublished.push_back({publication, retained});
				}
			}
		}

		for (auto it = pending_media_tracks_.begin(); it != pending_media_tracks_.end();) {
			if (FindRemoteParticipantForTrack(it->first)) {
				ready_tracks.push_back(it->second);
				it = pending_media_tracks_.erase(it);
			} else {
				++it;
			}
		}
	}

	if (auto* listener = event_listener_.load()) {
		for (const auto& participant : connected) {
			listener->OnParticipantConnected(participant.get());
		}
		for (const auto& event : published) {
			listener->OnTrackPublished(event.publication.get(), event.participant.get());
		}
		for (const auto& event : mute_changed) {
			if (event.muted) {
				listener->OnTrackMuted(event.publication.get(), event.participant);
			} else {
				listener->OnTrackUnmuted(event.publication.get(), event.participant);
			}
		}
		for (const auto& event : unpublished) {
			listener->OnTrackUnpublished(event.publication.get(), event.participant.get());
		}
		for (const auto& event : metadata_changed) {
			listener->OnParticipantMetadataChanged(event.value, event.participant);
		}
		for (const auto& event : name_changed) {
			listener->OnParticipantNameChanged(event.value, event.participant);
		}
		for (const auto& event : attributes_changed) {
			listener->OnParticipantAttributesChanged(event.changes, event.participant);
		}
		for (const auto& participant : disconnected) {
			listener->OnParticipantDisconnected(participant.get());
		}
	}

	for (auto& track : ready_tracks) {
		MediaTrackEvent(std::move(track));
	}
}

RoomInterface* CreateRoom() { return new Room(); }

std::unique_ptr<RoomInterface> CreateRoomUnique(RoomOptions options) {
	return std::make_unique<Room>(std::move(options));
}

} // namespace core
} // namespace livekit
