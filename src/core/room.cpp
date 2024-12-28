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

#include "livekit/core/room.h"
#include "detail/rtc_engine.h"
#include "detail/converted_proto.h"

namespace livekit {
namespace core {
Room::Room() { rtc_engine_ = std::make_unique<RtcEngine>(); }

Room::~Room() {}

bool Room::connect(std::string url, std::string token, RoomOptions options) {
	EngineOptions engine_options;
	livekit::JoinResponse join_response = rtc_engine_->connect(url, token, engine_options);
    if (join_response.room().name().empty()) {
		return false;
    }
    if (join_response.has_server_info()) {
		server_info_ = from_proto(join_response.server_info());
    }
	
	return true;
}
} // namespace core
} // namespace livekit
