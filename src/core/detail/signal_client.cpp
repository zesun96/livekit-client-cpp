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
#include <string>
#include <string_view>

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

	wsc_->set_recv_cb(std::bind(&SignalClient::on_ws_message, this, std::placeholders::_1));
	wsc_->set_event_cb(
	    std::bind(&SignalClient::on_ws_event, this, std::placeholders::_1, std::placeholders::_2));

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

void SignalClient::on_ws_message(std::shared_ptr<WebsocketData>& data) {
	if (data->type == WebsocketDataType::Binany) {
		std::cout << "WebSocket message, len:" << data->length << ", data:" << data->data
		          << std::endl;
	} else {
		std::cout << "WebSocket message, len:" << data->length
		          << ", data:" << std::string_view((char*)data->data, data->length) << std::endl;
	}

	return;
}

void SignalClient::on_ws_event(enum EventCode code, EventReason reason) {
	std::cout << "WebSocket event:" << int(code) << ", reason" << reason << std::endl;
	if (int(code) == 1) {
		std::lock_guard<std::mutex> guard(lock_);
		std::thread t1([this]() {
			Sleep(1000 * 5);
			std::string test_msg("{\"sender\":\"event a\",\"recipient\":\"\"}");
			std::unique_ptr<WebsocketData> data = std::make_unique<WebsocketData>(
			    (const void*)test_msg.c_str(), test_msg.length() * 5, WebsocketDataType::Text);
			this->wsc_->send(std::move(data));
		});
		t1.detach();
	}

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

// void SignalClient::on_error(std::string error) {
// 	std::cout << "WebSocket error: " << error << std::endl;
// 	std::lock_guard<std::mutex> guard(lock_);
// 	if (state_ != SignalConnectionState::CONNECTED) {
// 		state_ = SignalConnectionState::DISCONNECTED;
// 		promise_.set_value(livekit::JoinResponse());
// 	}
// 	return;
// }

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
