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

#ifndef _LKC_CORE_DETAIL_RTC_SESSION_H_
#define _LKC_CORE_DETAIL_RTC_SESSION_H_

#include "livekit/core/option/rtc_engine_option.h"
#include "peer_transport.h"
#include "livekit_rtc.pb.h"

namespace livekit {
namespace core {
class RtcSession {
public:
	RtcSession(livekit::JoinResponse join_response, EngineOptions options);
	~RtcSession();

	static std::unique_ptr<RtcSession>
	Create(livekit::JoinResponse join_response, EngineOptions options);

    bool Init();

private:
	livekit::JoinResponse join_response_;

    std::unique_ptr<PeerTransport> publisher_pc_;
	std::unique_ptr<PeerTransport> subscriber_pc_;
	EngineOptions options_;
	std::unique_ptr <PeerTransport::PrivateListener> private_listener_;
};

}
} // namespace livekit

#endif // _LKC_CORE_DETAIL_RTC_SESSION_H_
