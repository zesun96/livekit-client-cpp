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

#include "converted_proto.h"

namespace livekit {
namespace core {
ProtoJoinResponse from_proto(livekit::JoinResponse proto) {
	ProtoJoinResponse join_response;
	join_response.room.name = proto.room().name();
	return join_response;
}

livekit::JoinResponse to_proto(ProtoJoinResponse src) { return livekit::JoinResponse(); }

ServerInfo from_proto(livekit::ServerInfo proto) {
	ServerInfo server_info;
	server_info.node_id = proto.node_id();
	return server_info;
}

livekit::ServerInfo to_proto(ServerInfo src) { return livekit::ServerInfo(); }

} // namespace core
} // namespace livekit
