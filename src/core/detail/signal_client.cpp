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

#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>

namespace {

std::string serialize_sdp_error(webrtc::SdpParseError error) {
	std::stringstream ss;
	ss << std::hex << std::setfill('0');
	ss << std::setw(8) << (uint32_t)error.line.length();
	ss << std::dec << std::setw(1) << error.line;
	ss << std::dec << std::setw(1) << error.description;
	return ss.str();
}

std::unique_ptr<webrtc::SessionDescriptionInterface>
fromProtoSessionDescription(const livekit::SessionDescription& desc) {
	webrtc::SdpType type = webrtc::SdpType::kOffer;
	std::string proto_type = desc.type();
	if (proto_type == "offer") {
		type = webrtc::SdpType::kOffer;
	} else if (proto_type == "pranswer") {
		type = webrtc::SdpType::kPrAnswer;
	} else if (proto_type == "answer") {
		type = webrtc::SdpType::kAnswer;
	} else if (proto_type == "rollback") {
		type = webrtc::SdpType::kRollback;
	}
	webrtc::SdpParseError error;
	std::unique_ptr<webrtc::SessionDescriptionInterface> sessionDescription =
	    webrtc::CreateSessionDescription(type, desc.sdp().c_str(), &error);
	if (!sessionDescription) {
		throw std::runtime_error(serialize_sdp_error(error));
	}
	return sessionDescription;
}
} // namespace

namespace livekit {
namespace core {

SignalClient::SignalClient(std::string url, std::string token, SignalOptions option)
    : url_(url), token_(token), option_(option), state_(SignalConnectionState::DISCONNECTED),
      rtt_(0) {
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

void SignalClient::AddObserver(SignalClientObserver* observer) {
	observer_ = observer;
	return;
}

void SignalClient::RemoveObserver() { observer_ = nullptr; }

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
		auto sd = fromProtoSessionDescription(resp.answer());
		if (observer_) {
			observer_->OnAnswer(std::move(sd));
		}
		break;
	}
	case livekit::SignalResponse::MessageCase::kOffer: {
		auto sd = fromProtoSessionDescription(resp.answer());
		if (observer_) {
			observer_->OnOffer(std::move(sd));
		}
		break;
	}
	case livekit::SignalResponse::MessageCase::kTrickle: {
		std::string candidate = resp.trickle().candidateinit();
		auto target = resp.trickle().target();
		if (observer_) {
			observer_->OnTrickle(candidate, target);
		}
		break;
	}
	case livekit::SignalResponse::MessageCase::kUpdate: {
		if (observer_) {
			std::vector<livekit::ParticipantInfo> vec_participants;
			auto& proto_participants = resp.update().participants();
			for (auto& participant : proto_participants) {
				vec_participants.emplace_back(participant);
			}
			observer_->OnParticipantUpdate(vec_participants);
		}
		break;
	}
	case livekit::SignalResponse::MessageCase::kTrackPublished: {
		if (observer_) {
			auto& track_published = resp.track_published();
			observer_->OnLocalTrackPublished(track_published);
		}
		break;
	}
	case livekit::SignalResponse::MessageCase::kSpeakersChanged: {
		if (observer_) {
			std::vector<livekit::SpeakerInfo> vec_speakers;
			auto& proto_speakers = resp.speakers_changed().speakers();
			for (auto& speaker : proto_speakers) {
				vec_speakers.emplace_back(speaker);
			}
			observer_->OnSpeakersChanged(vec_speakers);
		}
		break;
	}
	case livekit::SignalResponse::MessageCase::kLeave: {
		if (observer_) {
			auto& leave_reauest = resp.leave();
			observer_->OnLeave(leave_reauest);
		}
		break;
	}
	case livekit::SignalResponse::MessageCase::kMute: {
		if (observer_) {
			auto& mute_request = resp.mute();
			observer_->OnRemoteMuteChanged(mute_request.sid(), mute_request.muted());
		}
		break;
	}
	case livekit::SignalResponse::MessageCase::kRoomUpdate: {
		if (observer_) {
			auto& room_update = resp.room_update();
			observer_->OnRoomUpdate(room_update.room());
		}
		break;
	}
	case livekit::SignalResponse::MessageCase::kConnectionQuality: {
		if (observer_) {
			std::vector<livekit::ConnectionQualityInfo> vec_quality_updates;
			auto& proto_updates = resp.connection_quality().updates();
			for (auto& update : proto_updates) {
				vec_quality_updates.emplace_back(update);
			}
			observer_->OnConnectionQuality(vec_quality_updates);
		}
		break;
	}
	case livekit::SignalResponse::MessageCase::kStreamStateUpdate: {
		if (observer_) {
			std::vector<livekit::StreamStateInfo> vec_stream_states;
			auto& proto_stream_states = resp.stream_state_update().stream_states();
			for (auto& stream_state : proto_stream_states) {
				vec_stream_states.emplace_back(stream_state);
			}
			observer_->OnStreamStateUpdate(vec_stream_states);
		}
		break;
	}
	case livekit::SignalResponse::MessageCase::kSubscribedQualityUpdate: {
		if (observer_) {
			auto& proto_subscribed_quality_updates = resp.subscribed_quality_update();
			observer_->OnSubscribedQualityUpdate(proto_subscribed_quality_updates);
		}
		break;
	}
	case livekit::SignalResponse::MessageCase::kSubscriptionPermissionUpdate: {
		if (observer_) {
			auto& proto_subscription_permission_updates = resp.subscription_permission_update();
			observer_->OnSubscriptionPermissionUpdate(proto_subscription_permission_updates);
		}
		break;
	}
	case livekit::SignalResponse::MessageCase::kRefreshToken: {
		if (observer_) {
			auto& refresh_token = resp.refresh_token();
			observer_->OnTokenRefresh(refresh_token);
		}
		break;
	}
	case livekit::SignalResponse::MessageCase::kTrackUnpublished: {
		if (observer_) {
			auto& track_unpublished = resp.track_unpublished();
			observer_->OnLocalTrackUnpublished(track_unpublished);
		}
		break;
	}
	case livekit::SignalResponse::MessageCase::kSubscriptionResponse: {
		if (observer_) {
			auto& subscription_response = resp.subscription_response();
			observer_->OnSubscriptionError(subscription_response);
		}
		break;
	}
	case livekit::SignalResponse::MessageCase::kPong: {
		break;
	}
	case livekit::SignalResponse::MessageCase::kPongResp: {
		auto lastPingTimestamp = resp.pong_resp().last_ping_timestamp();
		int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
		                  std::chrono::high_resolution_clock::now().time_since_epoch())
		                  .count();
		this->rtt_ = now - lastPingTimestamp;
		this->reset_ping_timeout();
		ping_handled = true;
		break;
	}
	case livekit::SignalResponse::MessageCase::kRequestResponse: {
		if (observer_) {
			auto& request_response = resp.request_response();
			observer_->OnRequestResponse(request_response);
		}
		break;
	}
	case livekit::SignalResponse::MessageCase::kTrackSubscribed: {
		if (observer_) {
			auto& track_sid = resp.track_subscribed().track_sid();
			observer_->OnLocalTrackSubscribed(track_sid);
		}
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
