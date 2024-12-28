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

#include "peer_transport.h"

namespace livekit {
namespace core {

PeerTransport::PeerTransport(livekit::JoinResponse join_response) : join_response_(join_response) {}

PeerTransport::~PeerTransport() {}

bool PeerTransport::Init() {
	auto& ice_servers = join_response_.ice_servers();
	for (auto& ice_server : ice_servers) {
	}
	return true;
}

std::unique_ptr<PeerTransport> PeerTransport::Create(livekit::JoinResponse join_response) {
	auto peer_transport = std::make_unique<PeerTransport>(join_response);
    if (!peer_transport->Init()) {
		return nullptr;
    }
	return peer_transport;
}
} // namespace core
} // namespace livekit
