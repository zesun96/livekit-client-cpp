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

#pragma once

#ifndef _LKC_CORE_ROOM_INTERFACE_H_
#define _LKC_CORE_ROOM_INTERFACE_H_

#include "option/room_option.h"
#include "participant/local_participant_interface.h"
#include "participant/participant_interface.h"
#include "participant/remote_participant_interface.h"
#include "protostruct/livekit_rtc_struct.h"
#include "room_event_interface.h"

#include <memory>

namespace livekit {
namespace core {

class RoomInterface {
public:
	enum class RoomState {
		Connecting,
		Connected,
		Disconnecting,
		Disconnected,
		Failed,
		Reconnecting,
	};

	virtual ~RoomInterface() = default;

	virtual bool Connect(std::string url, std::string token,
	                     RoomConnectOptions opts = default_room_connect_options()) = 0;

	virtual void AddEventListener(RoomEventInterface* listener) = 0;
	virtual void RemoveEventListener() = 0;

	// Returns the full connection lifecycle state. Unlike IsConnected(), this distinguishes
	// connecting, disconnecting, and failed rooms.
	RoomState State() const;
	virtual bool IsConnected() = 0;
	virtual bool Disconnect() = 0;
	virtual std::string Sid() { return {}; }
	virtual std::string Name() { return {}; }
	virtual std::string Metadata() { return {}; }
	virtual bool IsRecording() { return false; }

	virtual LocalParticipantInterface* GetLocalParticipant() = 0;
	virtual std::vector<RemoteParticipantInterface*> GetRemoteParticipants() = 0;
	virtual RemoteParticipantInterface* GetRemoteParticipantBySid(std::string sid) = 0;
	// LiveKit identity is stable for a participant session and should normally be preferred over
	// the mutable display name.
	RemoteParticipantInterface* GetRemoteParticipantByIdentity(std::string identity);
	virtual RemoteParticipantInterface* GetRemoteParticipantByName(std::string name) = 0;
	virtual std::vector<ParticipantInterface*> Participants() = 0;
	virtual ParticipantInterface* GetParticipantBySid(std::string sid) = 0;
	ParticipantInterface* GetParticipantByIdentity(std::string identity);
	virtual ParticipantInterface* GetParticipantByName(std::string name) = 0;

	// These controls return false when the room is disconnected or the participant/track SID does
	// not belong to the room.
	bool SetLocalTrackMuted(std::string track_sid, bool muted);
	bool SetRemoteTrackSubscribed(std::string participant_sid, std::string track_sid,
	                              bool subscribed);
};

RoomInterface* CreateRoom();
std::unique_ptr<RoomInterface> CreateRoomUnique(RoomOptions options = default_room_options());

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_ROOM_INTERFACE_H_
