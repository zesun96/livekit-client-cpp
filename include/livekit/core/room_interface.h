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
#include "participant/remote_participant__interface.h"
#include "protostruct/livekit_rtc_struct.h"

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
	};

	virtual ~RoomInterface() = default;

	virtual bool Connect(std::string url, std::string token,
	                     RoomConnectOptions opts = default_room_connect_options()) = 0;

	virtual bool IsConnected() = 0;
	virtual bool Disconnect() = 0;

	virtual LocalParticipantInterface* GetLocalParticipant() = 0;
	virtual std::vector<RemoteParticipantInterface*> GetRemoteParticipants() = 0;
	virtual RemoteParticipantInterface* GetRemoteParticipantBySid(std::string sid) = 0;
	virtual RemoteParticipantInterface* GetRemoteParticipantByName(std::string name) = 0;
	virtual std::vector<ParticipantInterface*> Participants() = 0;
	virtual ParticipantInterface* GetParticipantBySid(std::string sid) = 0;
	virtual ParticipantInterface* GetParticipantByName(std::string name) = 0;
};

RoomInterface* CreateRoom();

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_ROOM_INTERFACE_H_