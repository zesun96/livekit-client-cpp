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

#include <memory>
#include <string>
#include <future>
#include <websocketclient.hpp>

namespace livekit {
namespace core {

class SignalClient {
private:
public:
	enum SignalConnectionState {
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

	bool connect();

private:
	bool Init();
	void on_open();
	void on_message(std::variant<wsc::binary, wsc::string> message);
	void on_closed();
	void on_error(std::string error);

private:
	std::string url_;
	std::string token_;
	SignalOptions option_;
	std::unique_ptr<wsc::WebSocket> wsc_;
	std::atomic<SignalConnectionState> state_;
    std::promise<std::string> promise_;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_SIGNAL_CLIENT_H_
