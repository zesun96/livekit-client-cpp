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

#ifndef _LKC_CORE_DETAIL_WEBSOCKET_CLIENT_H_
#define _LKC_CORE_DETAIL_WEBSOCKET_CLIENT_H_

#include "websocket_uri.h"

#include "libwebsockets.h"

#include <chrono>
#include <optional>
#include <queue>
#include <string>
#include <vector>

namespace livekit {
namespace core {

struct WebsocketConnectionOptions {
	int retry_backoff_random_range_s;
	int retry_backoff_repeat_times;
	int retry_backoff_wait_minimum_s;
	int max_connection_attempts;
	int ping_interval_s;
	std::string ping_payload;
	int pong_timeout_s;
};

class WebsocketClient {
public:
	WebsocketClient(const WebsocketConnectionOptions& connection_options, WebsocketUri uri);
	~WebsocketClient();

	void connect();
	void disconnect();
	void send(std::string message);
	void service();

	static int callback_wrapper(struct lws* wsi, enum lws_callback_reasons reason, void* user,
	                            void* in, size_t len);

private:
	int happlay_cb(struct lws* wsi, enum lws_callback_reasons reason, void* in, size_t len);

	struct lws_protocols protocols[2] = {
	    {
	        .name = "rtc",
	        .callback = WebsocketClient::callback_wrapper, // make sure to set
	                                                       // lws_protocols.user to `this`
	        .per_session_data_size = 0,
	        .rx_buffer_size = 4096,
	        // .id = 0, // ignored by lws, i dont use it
	        // .user = this // idk where this even comes out
	    },
	    {NULL, NULL, 0, 0} // terminator
	};

	WebsocketConnectionOptions connection_options_;
	WebsocketUri uri_;
	size_t reconnect_attempts_ = 0;
	std::optional<std::chrono::time_point<std::chrono::steady_clock>> restart_after_ = std::nullopt;
	std::queue<std::string> msg_tx_queue_;
	std::vector<uint8_t> send_buffer_;
	bool conn_established_ = false;
	struct lws_context* context_ = nullptr;
	struct lws* wsi_ = nullptr;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_WEBSOCKET_CLIENT_H_
