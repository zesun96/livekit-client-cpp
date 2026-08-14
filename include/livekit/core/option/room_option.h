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

#ifndef _LKC_CORE_OPTION_ROOM_OPTION_H_
#define _LKC_CORE_OPTION_ROOM_OPTION_H_

#include "reconnect_policy.h"
#include "rtc_engine_option.h"

namespace livekit {
namespace core {

struct RoomSdkOptions {
	std::string sdk = "cpp";
	std::string sdk_version = "0.0.1";
};

struct RoomOptions {
	bool auto_subscribe = true;
	bool adaptive_stream = false;
	bool dynacast = false;
	RtcConfiguration rtc_config;
	uint32_t join_retries = 3;
	std::chrono::milliseconds reconnect_timeout{15'000};
	std::shared_ptr<ReconnectPolicy> reconnect_policy = CreateDefaultReconnectPolicy();
	RoomSdkOptions sdk_options;
};

RoomOptions default_room_options();

using RoomConnectOptions = RoomOptions;

RoomConnectOptions default_room_connect_options();

} // namespace core
} // namespace livekit

#endif //
