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

#include "livekit/core/livekit_client.h"
#include "detail/signal_client.h"

#include <api/peer_connection_interface.h>
#include <functional>
#include <iostream>
#include <memory>

namespace livekit {
namespace core {
void TestWebrtc() {
	webrtc::PeerConnectionInterface::IceTransportsType aa;
	return;
}

bool Test() {
	auto option = SignalClientOption();
	auto signal_client = SignalClient::Create("ws://localhost:8080/ws", "aaa", option);
	return true;
}
} // namespace core
} // namespace livekit
