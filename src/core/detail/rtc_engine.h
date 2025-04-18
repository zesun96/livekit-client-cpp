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

#ifndef _LKC_CORE_DETAIL_RTC_ENGINE_H_
#define _LKC_CORE_DETAIL_RTC_ENGINE_H_

#include "livekit/core/option/rtc_engine_option.h"
#include "livekit_rtc.pb.h"
#include "rtc_session.h"

#include <memory>
#include <string>

namespace livekit {
namespace core {

class RtcSession;

class SignalClient;
class RtcEngine {
public:
	RtcEngine();
	~RtcEngine();

	livekit::JoinResponse Connect(std::string url, std::string token, EngineOptions options);

private:
	std::unique_ptr<SignalClient> signal_client_;
	std::unique_ptr<RtcSession> rtc_session_;
};

} // namespace core
} // namespace livekit

#endif //
