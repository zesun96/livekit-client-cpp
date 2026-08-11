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

#include "websocket_uri.h"

#include <stdexcept>
#include <utility>

namespace livekit {
namespace core {

WebsocketUri::WebsocketUri(Url url) : url_(std::move(url)), secure_(url_.GetScheme() == "wss") {}

WebsocketUri WebsocketUri::parse_and_validate(const std::string& uri) {
	if (uri.empty()) {
		throw std::invalid_argument("WebSocket URI must not be empty");
	}

	Url url(uri);
	if (url.GetScheme() == "http") {
		url.SetScheme("ws");
	} else if (url.GetScheme() == "https") {
		url.SetScheme("wss");
	} else if (url.GetScheme() != "ws" && url.GetScheme() != "wss") {
		throw std::invalid_argument("WebSocket URI scheme must be ws, wss, http, or https");
	}
	if (url.GetHost().empty()) {
		throw std::invalid_argument("WebSocket URI must include a host");
	}
	if (url.GetPort() == 0) {
		url.SetPort(url.GetScheme() == "wss" ? 443 : 80);
	}
	return WebsocketUri(std::move(url));
}

} // namespace core
} // namespace livekit
