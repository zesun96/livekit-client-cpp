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

#include "websocket_data.h"

namespace livekit {
namespace core {
WebsocketData::WebsocketData(const void* in, uint32_t length, WebsocketDataType type)
    : length(length), type(type) {
	if (length > 0) {
		data = new int8_t[length]{0};
		memcpy(data, in, length);
	} else {
		throw "buf size need greater than zero";
	}
}
WebsocketData::~WebsocketData() { release(); }

void WebsocketData::release() {
	if (data != nullptr) {
		delete[] data;
		data = nullptr;
	}
	length = 0;
}

void WebsocketData::copy_from(WebsocketData* ws_data) {
	data = new int8_t[ws_data->length];
	memcpy(data, ws_data->data, ws_data->length);
	length = ws_data->length;
	type = ws_data->type;
}

} // namespace core
} // namespace livekit
