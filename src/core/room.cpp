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

	rtc_engine_->Disconnect();
	{
		std::lock_guard<std::mutex> guard(participants_mutex_);
		remote_participants_.clear();
	}
	state_ = RoomState::Disconnected;
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

void Room::ApplyParticipantUpdates(const std::vector<livekit::ParticipantInfo>& updates) {
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
			remote_participants_.emplace(info.sid(), std::make_unique<RemoteParticipant>(info));
		} else {
			participant->second->UpdateFromInfo(info);
		}
	}
}

RoomInterface* CreateRoom() { return new Room(); }

std::unique_ptr<RoomInterface> CreateRoomUnique(RoomOptions options) {
	return std::make_unique<Room>(std::move(options));
}

} // namespace core
} // namespace livekit
