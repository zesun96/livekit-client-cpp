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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace livekit {
namespace core {

enum class EventCode { Unknown, Connected, Disconnected };

using EventReason = std::string;

enum class WebsocketDataType { Unknown, Text, Binary };

class WebsocketData final {
public:
	WebsocketData() = default;
	WebsocketData(const void* data, std::size_t size, WebsocketDataType type);

	const std::uint8_t* data() const noexcept { return bytes_.data(); }
	std::size_t size() const noexcept { return bytes_.size(); }
	bool empty() const noexcept { return bytes_.empty(); }
	WebsocketDataType type() const noexcept { return type_; }

private:
	std::vector<std::uint8_t> bytes_;
	WebsocketDataType type_ = WebsocketDataType::Text;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_WEBSOCKET_DATA_H_
