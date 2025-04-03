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
		std::cout << "WebSocket binany message, len:" << data->length << std::endl;
		handle_ws_binany_message(data);
	} else {
		std::cout << "WebSocket message, len:" << data->length
		          << ", data:" << std::string_view((char*)data->data, data->length) << std::endl;
	}

	return;
}

void SignalClient::on_ws_event(enum EventCode code, EventReason reason) {
	std::cout << "WebSocket event:" << int(code) << ", reason" << reason << std::endl;
	std::lock_guard<std::mutex> guard(lock_);
	if (code == EventCode::Connected) {

	} else if (code == EventCode::DisConnected) {
		if (state_ != SignalConnectionState::CONNECTED) {
			state_ = SignalConnectionState::DISCONNECTED;
			promise_.set_value(livekit::JoinResponse());
		}
	}
	return;
}

void SignalClient::handle_ws_binany_message(std::shared_ptr<WebsocketData>& data) {
	std::lock_guard<std::mutex> guard(lock_);
	livekit::SignalResponse resp{};
	bool ret = resp.ParseFromArray(data->data, data->length);
	if (ret) {
		std::cout << "SignalResponsecase(: " << resp.message_case() << std::endl;
		if (state_ != SignalConnectionState::CONNECTED) {
			bool should_process_message = false;
			switch (resp.message_case()) {
			case livekit::SignalResponse::MessageCase::kJoin:
				state_ = SignalConnectionState::CONNECTED;

				ping_timeout_duration_ = resp.join().ping_timeout();
				ping_interval_duration_ = resp.join().ping_interval();

				if (ping_timeout_duration_ > 0) {
					std::cout << "ping config" << ping_timeout_duration_ << ", "
					          << ping_interval_duration_ << std::endl;
					this->start_ping_interval();
				}

				promise_.set_value(resp.join());
				break;
			case livekit::SignalResponse::MessageCase::kLeave:
				if (is_establishing_connection()) {
					promise_.set_value(livekit::JoinResponse());
				}
				break;
			default:
				if (state_ == SignalConnectionState::RECONNECTING) {
					state_ = SignalConnectionState::CONNECTED;
					this->start_ping_interval();
					if (resp.message_case() == livekit::SignalResponse::MessageCase::kReconnect) {
						promise_.set_value(resp.join());
					} else {
						promise_.set_value(livekit::JoinResponse());
						should_process_message = true;
					}
				} else if (!option_.reconnect) {

					break;
				}
			}
			if (!should_process_message) {
				return;
			}
		}

		this->handle_signal_response(resp);
	}

	return;
}

bool SignalClient::is_establishing_connection() {
	return (state_ == SignalConnectionState::CONNECTING ||
	        state_ == SignalConnectionState::RECONNECTING);
}

void SignalClient::handle_signal_response(livekit::SignalResponse& resp) {
	bool ping_handled = false;
	switch (resp.message_case()) {
	case livekit::SignalResponse::MessageCase::kAnswer: {
		break;
	}
	case livekit::SignalResponse::MessageCase::kOffer: {
		break;
	}
	case livekit::SignalResponse::MessageCase::kTrickle: {
		break;
	}
	case livekit::SignalResponse::MessageCase::kUpdate: {
		break;
	}
	case livekit::SignalResponse::MessageCase::kTrackPublished: {
		break;
	}
	case livekit::SignalResponse::MessageCase::kSpeakersChanged: {
		break;
	}
	case livekit::SignalResponse::MessageCase::kLeave: {
		break;
	}
	case livekit::SignalResponse::MessageCase::kMute: {
		break;
	}
	case livekit::SignalResponse::MessageCase::kRoomUpdate: {
		break;
	}
	case livekit::SignalResponse::MessageCase::kConnectionQuality: {
		break;
	}
	case livekit::SignalResponse::MessageCase::kStreamStateUpdate: {
		break;
	}
	case livekit::SignalResponse::MessageCase::kSubscribedQualityUpdate: {
		break;
	}
	case livekit::SignalResponse::MessageCase::kSubscriptionPermissionUpdate: {
		break;
	}
	case livekit::SignalResponse::MessageCase::kRefreshToken: {
		break;
	}
	case livekit::SignalResponse::MessageCase::kTrackUnpublished: {
		break;
	}
	case livekit::SignalResponse::MessageCase::kSubscriptionResponse: {
		break;
	}
	case livekit::SignalResponse::MessageCase::kPong: {
		break;
	}
	case livekit::SignalResponse::MessageCase::kPongResp: {
		this->reset_ping_timeout();
		ping_handled = true;
		break;
	}
	case livekit::SignalResponse::MessageCase::kRequestResponse: {
		break;
	}
	case livekit::SignalResponse::MessageCase::kTrackSubscribed: {
		break;
	}
	default: {
		std::cout << "unsupported message(: " << resp.message_case() << std::endl;
	}
	}

	if (!ping_handled) {
		this->reset_ping_timeout();
	}
}

void SignalClient::reset_ping_timeout() {
	this->clear_ping_timeout();
	return;
}

void SignalClient::clear_ping_timeout() { return; }

void SignalClient::start_ping_interval() {
	this->clear_ping_interval();
	this->reset_ping_timeout();
	return;
}

void SignalClient::clear_ping_interval() { return; }

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
