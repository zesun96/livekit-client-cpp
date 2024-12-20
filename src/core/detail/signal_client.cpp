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

#include <functional>

namespace livekit {
namespace core {

SignalClient::SignalClient(std::string url, std::string token, SignalOptions option)
    : url_(url), token_(token), option_(option) {
	wsc_ = std::make_unique<wsc::WebSocket>();
}

SignalClient::~SignalClient() {}

bool SignalClient::Init() {
	auto on_open = std::bind(&SignalClient::on_open, this);
	wsc_->onOpen(on_open);

	auto on_message = std::bind(&SignalClient::on_message, this, std::placeholders::_1);
	wsc_->onMessage(on_message);

	auto on_closed = std::bind(&SignalClient::on_closed, this);
	wsc_->onClosed(on_closed);

	auto on_error = std::bind(&SignalClient::on_error, this, std::placeholders::_1);
	wsc_->onError(on_error);
	return true;
}

bool SignalClient::connect() {
	std::string request = url_ + "?token=" + token_ +
	                      "&auto_subscribe=1&sdk=cpp&version=0.0.1&protocol=15&adaptive_stream=1";
	wsc_->open(request);
	return wsc_->isOpen();
}

void SignalClient::on_open() {
	std::cout << "WebSocket open" << std::endl;
	return;
}

void SignalClient::on_message(std::variant<wsc::binary, wsc::string> message) {
	std::cout << "WebSocket recived message" << std::endl;
	if (std::holds_alternative<wsc::binary>(message)) {
		auto& msg = std::get<wsc::binary>(message);
		std::cout << "WebSocket received: " << msg.size() << std::endl;
	}
	return;
}
void SignalClient::on_closed() {
	std::cout << "WebSocket closed" << std::endl;
	return;
}

void SignalClient::on_error(std::string error) {
	std::cout << "WebSocket error: " << error << std::endl;
	return;
}

std::unique_ptr<SignalClient> SignalClient::Create(std::string url, std::string token,
                                                   SignalOptions option) {
	auto signal_client = std::make_unique<SignalClient>(url, token, option);
	signal_client->Init();
	return signal_client;
}

} // namespace core
} // namespace livekit
