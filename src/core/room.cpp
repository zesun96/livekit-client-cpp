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
#include "track/video_track.h"

#include <limits>

namespace {
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
		ApplyParticipantUpdates(participants);
	} catch (...) {
		state_ = RoomState::Failed;
		throw;
	}

	return true;
}

void Room::AddEventListener(RoomEventInterface* listener) { event_listener_.store(listener); }

void Room::RemoveEventListener() { event_listener_.store(nullptr); }

bool Room::IsConnected() { return state_.load() == RoomState::Connected; }

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
		std::lock_guard<std::mutex> guard(incoming_files_mutex_);
		incoming_files_.clear();
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

void Room::ConnectedEvent(livekit::JoinResponse join_resp) {
	state_ = RoomState::Connected;
	if (auto* listener = event_listener_.load()) {
		listener->OnConnected();
	}
}

void Room::ParticipantUpdateEvent(const std::vector<livekit::ParticipantInfo>& updates) {
	ApplyParticipantUpdates(updates);
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
	if (packet.has_stream_header() && packet.stream_header().has_byte_header()) {
		const auto& header = packet.stream_header();
		if (header.stream_id().empty()) {
			return;
		}
		IncomingFile incoming;
		incoming.event.stream_id = header.stream_id();
		incoming.event.name = header.byte_header().name();
		incoming.event.mime_type = header.mime_type();
		incoming.event.topic = header.topic();
		incoming.event.participant_identity = packet.participant_identity();
		incoming.expected_length = header.has_total_length() ? header.total_length() : 0;
		if (incoming.expected_length > 0) {
			if (incoming.expected_length > std::numeric_limits<std::size_t>::max()) {
				return;
			}
			try {
				incoming.event.data.reserve(static_cast<std::size_t>(incoming.expected_length));
			} catch (const std::exception&) {
				return;
			}
		}
		std::lock_guard<std::mutex> guard(incoming_files_mutex_);
		incoming_files_[header.stream_id()] = std::move(incoming);
		return;
	}
	if (packet.has_stream_chunk()) {
		const auto& chunk = packet.stream_chunk();
		std::lock_guard<std::mutex> guard(incoming_files_mutex_);
		auto it = incoming_files_.find(chunk.stream_id());
		if (it == incoming_files_.end() || chunk.chunk_index() != it->second.next_chunk) {
			return;
		}
		const auto content_size = static_cast<uint64_t>(chunk.content().size());
		if (it->second.expected_length != 0 &&
		    (it->second.event.data.size() > it->second.expected_length ||
		     content_size > it->second.expected_length - it->second.event.data.size())) {
			incoming_files_.erase(it);
			return;
		}
		it->second.event.data.insert(it->second.event.data.end(), chunk.content().begin(),
		                             chunk.content().end());
		++it->second.next_chunk;
		return;
	}
	if (packet.has_stream_trailer()) {
		FileReceivedEvent event;
		{
			std::lock_guard<std::mutex> guard(incoming_files_mutex_);
			auto it = incoming_files_.find(packet.stream_trailer().stream_id());
			if (it == incoming_files_.end() || !packet.stream_trailer().reason().empty() ||
			    (it->second.expected_length != 0 &&
			     it->second.event.data.size() != it->second.expected_length)) {
				return;
			}
			event = std::move(it->second.event);
			incoming_files_.erase(it);
		}
		if (auto* listener = event_listener_.load()) {
			listener->OnFileReceived(event);
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

void Room::ApplyParticipantUpdates(const std::vector<livekit::ParticipantInfo>& updates) {
	std::vector<webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface>> ready_tracks;
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		for (const auto& info : updates) {
			if (info.sid().empty()) {
				continue;
			}
			if (info.sid() == local_participant_->Sid() ||
			    (!info.identity().empty() && info.identity() == local_participant_->Identity())) {
				local_participant_->UpdateFromInfo(info);
				continue;
			}
			auto participant = remote_participants_.find(info.sid());
			if (info.state() == livekit::ParticipantInfo_State_DISCONNECTED) {
				if (participant != remote_participants_.end()) {
					remote_participants_.erase(participant);
				}
				continue;
			}
			if (participant == remote_participants_.end()) {
				remote_participants_.emplace(info.sid(), std::make_shared<RemoteParticipant>(info));
			} else {
				participant->second->UpdateFromInfo(info);
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
