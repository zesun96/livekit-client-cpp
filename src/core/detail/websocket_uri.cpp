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

namespace livekit {
namespace core {

WebsocketUri::WebsocketUri(Url url) : url_(url) {}

WebsocketUri::~WebsocketUri() {}

WebsocketUri WebsocketUri::parse_and_validate(std::string uri, std::string chargepoint_id,
                                              int security_profile) {
	if (uri.empty()) {
		throw std::invalid_argument("`uri`-parameter must not be empty");
	}
	// if (chargepoint_id.empty()) {
	// 	throw std::invalid_argument("`chargepoint_id`-parameter must not be empty");
	// }
	auto tmp = WebsocketUri(Url(uri));
	return tmp;
}

} // namespace core
} // namespace livekit
