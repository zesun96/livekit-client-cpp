/**
 *
 * Copyright (c) 2025 sunze
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

#ifndef _LKC_CORE_DETAIL_WEBSOCKET_DATA_H_
#define _LKC_CORE_DETAIL_WEBSOCKET_DATA_H_

#include <stdint.h>
#include <string>

namespace livekit {
namespace core {

enum class EventCode { Unknown, Connected, DisConnected };

using EventReason = std::string;

enum class WebsocketDataType { Unknown, Text, Binany };

struct WebsocketData {
	WebsocketData() = default;
	WebsocketData(const void* in, uint32_t length, WebsocketDataType type);
	virtual ~WebsocketData();
	WebsocketData(const WebsocketData&) = delete;
	WebsocketData& operator=(const WebsocketData&) = delete;

	void release();
	void copy_from(WebsocketData* ws_data);

	int8_t* data = nullptr;
	WebsocketDataType type = WebsocketDataType::Text;
	uint32_t length = 0;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_WEBSOCKET_DATA_H_
