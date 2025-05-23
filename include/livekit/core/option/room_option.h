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

#include "rtc_engine_option.h"

namespace livekit {
namespace core {

struct RoomSdkOptions {
	std::string sdk;
	std::string sdk_version;
};

struct RoomOptions {
	bool auto_subscribe;
	bool adaptive_stream;
	bool dynacast;
	RtcConfiguration rtc_config;
	uint32_t join_retries;
	RoomSdkOptions sdk_options;
};

RoomOptions default_room_options();

using RoomConnectOptions = RoomOptions;

RoomConnectOptions default_room_connect_options();

} // namespace core
} // namespace livekit

#endif //
