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

#include "rtc_engine.h"
#include "internals.h"
#include "rtc_session.h"
#include "signal_client.h"

namespace livekit {
namespace core {

RtcEngine::RtcEngine() {}

RtcEngine::~RtcEngine() { std::cout << "RtcEngine::~RtcEngine()" << std::endl; }

livekit::JoinResponse RtcEngine::connect(std::string url, std::string token,
                                         EngineOptions options) {
	signal_client_ = SignalClient::Create(url, token, options.signal_options);
	livekit::JoinResponse response = signal_client_->connect();
	PLOG_DEBUG << "received JoinResponse: " << response.room().name();
	if (response.has_room()) {
		rtc_session_ = RtcSession::Create(response, options);
	}

	return response;
}

} // namespace core
} // namespace livekit
