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

#ifndef _LKC_CORE_ROOM_H_
#define _LKC_CORE_ROOM_H_

#include "livekit/core/room_interface.h"

#include "participant/local_participant.h"
#include "participant/remote_participant.h"

#include <map>
#include <memory>

namespace livekit {
namespace core {

class RtcEngine;

class Room : public RoomInterface {
public:
	Room();
	virtual ~Room();

	bool Connect(std::string url, std::string token, RoomOptions options = default_room_options());

private:
	std::unique_ptr<RtcEngine> rtc_engine_;
	std::unique_ptr<LocalParticipant> local_participant_;
	std::map<std::string, std::unique_ptr<RemoteParticipant>> remote_participants_;
	ServerInfo server_info_;
};

} // namespace core
} // namespace livekit

#endif //
