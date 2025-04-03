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

#ifndef _LKC_CORE_DETAIL_SIGNAL_CLIENT_H_
#define _LKC_CORE_DETAIL_SIGNAL_CLIENT_H_

#include "livekit/core/option/signal_option.h"
#include "livekit_models.pb.h"
#include "livekit_rtc.pb.h"
#include "websocket_client.h"

#include <future>
#include <memory>
#include <mutex>
#include <string>

namespace livekit {
namespace core {

class JoinRespone;

class SignalClient {
private:
public:
	enum class SignalConnectionState {
		CONNECTING,
		CONNECTED,
		RECONNECTING,
		DISCONNECTING,
		DISCONNECTED,
	};

	static std::unique_ptr<SignalClient> Create(std::string url, std::string token,
	                                            SignalOptions option);

	SignalClient(std::string url, std::string token, SignalOptions option);
	~SignalClient();

	livekit::JoinResponse connect();

private:
	bool Init();
	void on_ws_message(std::shared_ptr<WebsocketData>& data);
	void on_ws_event(enum EventCode code, EventReason reason);
	void handle_ws_binany_message(std::shared_ptr<WebsocketData>& data);
	bool is_establishing_connection();
	void handle_signal_response(livekit::SignalResponse& resp);
	void reset_ping_timeout();
	void clear_ping_timeout();
	void start_ping_interval();
	void clear_ping_interval();

private:
	std::string url_;
	std::string token_;
	SignalOptions option_;
	mutable std::mutex lock_;
	std::unique_ptr<WebsocketClient> wsc_;
	std::atomic<SignalConnectionState> state_;
	std::promise<livekit::JoinResponse> promise_;
	int ping_timeout_duration_;
	int ping_interval_duration_;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_SIGNAL_CLIENT_H_
