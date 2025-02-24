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

#include "signal_client.h"
#include "livekit_models.pb.h"
#include "livekit_rtc.pb.h"
#include "websocket_uri.h"

#include <functional>
#include <iostream>

namespace livekit {
namespace core {

SignalClient::SignalClient(std::string url, std::string token, SignalOptions option)
    : url_(url), token_(token), option_(option), state_(SignalConnectionState::DISCONNECTED) {
	std::string request = url_ + "?access_token=" + token_ +
	                      "&auto_subscribe=1&sdk=cpp&version=0.0.1&protocol=15&adaptive_stream=1";
	WebsocketConnectionOptions ws_option;
	wsc_ = std::make_unique<WebsocketClient>(ws_option, request);

	return;
}

SignalClient::~SignalClient() {
	std::cout << "SignalClient::~SignalClient()" << std::endl;
	wsc_->disconnect();
}

bool SignalClient::Init() {
	auto on_open = std::bind(&SignalClient::on_open, this);
	// wsc_->onOpen(on_open);

	// auto on_message = std::bind(&SignalClient::on_message, this, std::placeholders::_1);
	// wsc_->onMessage(on_message);

	auto on_closed = std::bind(&SignalClient::on_closed, this);
	// wsc_->onClosed(on_closed);

	auto on_error = std::bind(&SignalClient::on_error, this, std::placeholders::_1);
	// wsc_->onError(on_error);
	return true;
}

livekit::JoinResponse SignalClient::connect() {
	state_ = SignalConnectionState::CONNECTING;

	wsc_->connect();
	wsc_->service();
	std::future<livekit::JoinResponse> future = promise_.get_future();
	try {
		livekit::JoinResponse response = future.get();
		return response;
	} catch (const std::exception& e) {
		std::cerr << "Error: " << e.what() << std::endl;
	}
	return livekit::JoinResponse();
}

void SignalClient::on_open() {
	std::cout << "WebSocket open" << std::endl;
	std::lock_guard<std::mutex> guard(lock_);
	return;
}

// void SignalClient::on_message(std::variant<wsc::binary, wsc::string> message) {
//	std::cout << "WebSocket recived message" << std::endl;
//	std::lock_guard<std::mutex> guard(lock_);
//	livekit::SignalResponse resp{};
//	if (std::holds_alternative<wsc::string>(message)) {
//		auto& msg = std::get<wsc::string>(message);
//		std::cout << "WebSocket str received size: " << msg.size() << std::endl;
//	} else if (std::holds_alternative<wsc::binary>(message)) {
//		auto& msg = std::get<wsc::binary>(message);
//		std::cout << "WebSocket binary received size: " << msg.size() << std::endl;
//		std::cout << "WebSocket binary received: " << msg.data() << std::endl;
//		size_t len = msg.size() * sizeof(std::byte);
//		bool ret = resp.ParseFromArray(msg.data(), len);
//		if (ret) {
//			std::cout << "SignalResponsecase(: " << resp.message_case() << std::endl;
//			if (state_ != SignalConnectionState::CONNECTED) {
//				switch (resp.message_case()) {
//				case livekit::SignalResponse::MessageCase::kJoin:
//					state_ = SignalConnectionState::CONNECTED;
//					promise_.set_value(resp.join());
//					break;
//				case livekit::SignalResponse::MessageCase::kLeave:
//					if (is_establishing_connection()) {
//						promise_.set_value(livekit::JoinResponse());
//					}
//					break;
//				default:
//					break;
//				}
//			}
//		}
//	} else {
//		std::cout << "could not decode websocket message" << std::endl;
//	}
//	if (state_ != SignalConnectionState::CONNECTED) {
//	}
//	return;
// }

void SignalClient::on_closed() {
	std::cout << "WebSocket closed" << std::endl;
	std::lock_guard<std::mutex> guard(lock_);
	return;
}

void SignalClient::on_error(std::string error) {
	std::cout << "WebSocket error: " << error << std::endl;
	std::lock_guard<std::mutex> guard(lock_);
	if (state_ != SignalConnectionState::CONNECTED) {
		state_ = SignalConnectionState::DISCONNECTED;
		promise_.set_value(livekit::JoinResponse());
	}
	return;
}

bool SignalClient::is_establishing_connection() {
	return (state_ == SignalConnectionState::CONNECTING ||
	        state_ == SignalConnectionState::RECONNECTING);
}

std::unique_ptr<SignalClient> SignalClient::Create(std::string url, std::string token,
                                                   SignalOptions option) {
	auto signal_client = std::make_unique<SignalClient>(url, token, option);
	if (!signal_client->Init()) {
		return nullptr;
	}
	return signal_client;
}

} // namespace core
} // namespace livekit
