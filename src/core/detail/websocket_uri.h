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

#ifndef _LKC_CORE_DETAIL_WEBSOCKET_URI_H_
#define _LKC_CORE_DETAIL_WEBSOCKET_URI_H_

#include "uri.h"

#include <stdint.h>
#include <string>

namespace livekit {
namespace core {
class WebsocketUri {
public:
	static WebsocketUri parse_and_validate(const std::string& uri);

public:
	explicit WebsocketUri(Url url);
	WebsocketUri() = default;

	const std::string& get_hostname() const noexcept { return url_.GetHost(); }

	uint16_t get_port() const noexcept { return url_.GetPort(); }

	std::string get_relative_url() const { return url_.GetRelativeUrl(); }

	bool is_secure() const noexcept { return secure_; }

private:
	Url url_;
	bool secure_ = false;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_WEBSOCKET_URI_H_
