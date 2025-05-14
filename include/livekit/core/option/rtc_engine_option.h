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

#ifndef _LKC_CORE_OPTION_RTC_ENGINE_OPTION_H_
#define _LKC_CORE_OPTION_RTC_ENGINE_OPTION_H_

#include "signal_option.h"

#include <stdint.h>
#include <vector>

namespace livekit {
namespace core {
struct IceServer {
	std::vector<std::string> urls;
	std::string username;
	std::string password;
};

enum class ContinualGatheringPolicy {
	GatherOnce,
	GatherContinually,
};

enum class IceTransportsType { None, Relay, NoHost, All };

struct RtcConfiguration {
	std::vector<IceServer> ice_servers;
	ContinualGatheringPolicy continual_gathering_policy;
	IceTransportsType ice_transport_type;
};

struct EngineOptions {
	RtcConfiguration rtc_config;
	SignalOptions signal_options;
	uint32_t join_retries;
};

} // namespace core
} // namespace livekit

#endif //
