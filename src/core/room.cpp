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
	local_participant_ = std::make_unique<LocalParticipant>("", "", rtc_engine_.get(), options_);
}

Room::~Room() {}

bool Room::Connect(std::string url, std::string token, RoomConnectOptions opts) {
	EngineOptions engine_options = make_engine_config(opts);
	livekit::JoinResponse join_response = rtc_engine_->Connect(url, token, engine_options);
	if (join_response.room().name().empty()) {
		return false;
	}
	if (join_response.has_server_info()) {
		server_info_ = from_proto(join_response.server_info());
	} else {
		server_info_.region = join_response.server_region();
		server_info_.version = join_response.server_version();
	}

	return true;
}

bool Room::IsConnected() { return this->state_ == RoomState::Connected; }

bool Room::Disconnect() {
	if (this->state_ == RoomState::Disconnecting || this->state_ == RoomState::Disconnected) {
		return false;
	}
	this->state_ = RoomState::Disconnecting;

	// do disconnect
	this->state_ = RoomState::Disconnected;
	return true;
}

LocalParticipantInterface* Room::GetLocalParticipant() { return this->local_participant_.get(); }

std::vector<RemoteParticipantInterface*> Room::GetRemoteParticipants() {
	std::vector<RemoteParticipantInterface*> participants;
	for (auto& participant : this->remote_participants_) {
		participants.push_back(participant.second.get());
	}
	return participants;
}

RemoteParticipantInterface* Room::GetRemoteParticipantBySid(std::string sid) {
	for (auto& participant : this->remote_participants_) {
		if (participant.first == sid) {
			return participant.second.get();
		}
	}
	return nullptr;
}

RemoteParticipantInterface* Room::GetRemoteParticipantByName(std::string name) {
	for (auto& participant : this->remote_participants_) {
		if (participant.second->Name() == name) {
			return participant.second.get();
		}
	}
	return nullptr;
}

std::vector<ParticipantInterface*> Room::Participants() {
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
	return dynamic_cast<ParticipantInterface*>(this->GetRemoteParticipantBySid(sid));
}

ParticipantInterface* Room::GetParticipantByName(std::string name) {
	if (this->local_participant_->Name() == name) {
		return this->local_participant_.get();
	}
	return dynamic_cast<ParticipantInterface*>(this->GetRemoteParticipantByName(name));
}

void Room::ConnectedEvent(livekit::JoinResponse join_resp) {}

RoomInterface* CreateRoom() { return new Room(); }

} // namespace core
} // namespace livekit
