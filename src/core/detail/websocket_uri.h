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
	static WebsocketUri parse_and_validate(std::string uri, std::string chargepoint_id = "",
	                                       int security_profile = 0);

public:
	WebsocketUri(Url url);
	~WebsocketUri();

	const std::string& get_hostname() { return url_.GetHost(); }

	uint16_t get_port() { return url_.GetPort(); }

	const std::string get_relative_url() { return url_.GetRelativeUrl(); }

private:
	Url url_;
	bool secure_;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_WEBSOCKET_URI_H_