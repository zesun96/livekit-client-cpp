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

#include "livekit/core/livekit_client_test.h"
#include "detail/signal_client.h"

#include <api/peer_connection_interface.h>

#include <functional>
#include <iostream>
#include <memory>

namespace livekit {
namespace core {
static bool make_rtc_config_join2() {
	// webrtc::PeerConnectionInterface::RTCConfigurationTest111 test11{};
	webrtc::PeerConnectionInterface::RTCConfiguration rtc_config(
	    webrtc::PeerConnectionInterface::RTCConfigurationType::kSafe);
	std::string str;
	rtc_config.turn_logging_id = str;

	return true;
}

void TestWebrtc() {
	auto cpu = make_rtc_config_join2();
	webrtc::PeerConnectionInterface::RTCConfigurationTest111 test11{};
	return;
}

bool Test() {
	auto option = SignalOptions();
	auto signal_client = SignalClient::Create("ws://localhost:8080/ws", "aaa", option);
	signal_client->Connect();
	while (true) {
	}
	return true;
}
} // namespace core
} // namespace livekit
