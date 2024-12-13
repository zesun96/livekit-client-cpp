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

namespace livekit {
namespace core {

SignalClient::SignalClient(std::string url, std::string token, SignalOptions option)
    : url_(url), token_(token), option_(option) {
	wsc_ = std::make_unique<wsc::WebSocket>();
}

SignalClient::~SignalClient() {}

bool SignalClient::Init() {

	wsc_->open(url_);
	return true;
}

bool SignalClient::connect() {
	wsc_->open(url_);
	return wsc_->isOpen();
}

std::unique_ptr<SignalClient> SignalClient::Create(std::string url, std::string token,
                                                   SignalOptions option) {
	auto signal_client = std::make_unique<SignalClient>(url, token, option);
	signal_client->Init();
	return signal_client;
}

} // namespace core
} // namespace livekit
