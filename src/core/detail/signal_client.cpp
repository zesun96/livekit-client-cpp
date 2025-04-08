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
#include "utils.h"
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
	const std::string sdp = desc.sdp();
	if (sdp.empty()) {
		return nullptr;
	}
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
	    webrtc::CreateSessionDescription(type, sdp, &error);
	if (sessionDescription == nullptr) {
		throw std::runtime_error(serialize_sdp_error(error));
	}
	return sessionDescription;
}

livekit::SessionDescription
toProtoSessionDescription(std::string mode,
                          std::unique_ptr<webrtc::SessionDescriptionInterface> desc) {
	livekit::SessionDescription proto_desc;
	auto description = desc->description();
	std::string str_desc;
	desc->ToString(&str_desc);
	proto_desc.set_sdp(str_desc);
	proto_desc.set_type(mode);
	return proto_desc;
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

bool SignalClient::init() {

	wsc_->set_recv_cb(std::bind(&SignalClient::onWsMessage, this, std::placeholders::_1));
	wsc_->set_event_cb(
	    std::bind(&SignalClient::onWsEvent, this, std::placeholders::_1, std::placeholders::_2));

	return true;
}

void SignalClient::AddObserver(SignalClientObserver* observer) {
	observer_ = observer;
	return;
}

void SignalClient::RemoveObserver() { observer_ = nullptr; }

livekit::JoinResponse SignalClient::Connect() {
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

void SignalClient::Close(bool update_state) {}

void SignalClient::SendOffer(std::unique_ptr<webrtc::SessionDescriptionInterface> offer) {
	livekit::SignalRequest request;
	auto* offer_msg = request.mutable_offer();
	auto proto_offer = toProtoSessionDescription("answer", std::move(offer));
	offer_msg->CopyFrom(proto_offer);
	sendRequest(request);
	return;
}

void SignalClient::SendAnswer(std::unique_ptr<webrtc::SessionDescriptionInterface> answer) {
	livekit::SignalRequest request;
	auto* answer_msg = request.mutable_answer();
	auto proto_answer = toProtoSessionDescription("answer", std::move(answer));
	answer_msg->CopyFrom(proto_answer);
	sendRequest(request);
	return;
}

void SignalClient::SendIceCandidate(std::string& candidate, livekit::SignalTarget target) {
	livekit::SignalRequest request;
	auto* trickle_msg = request.mutable_trickle();
	trickle_msg->set_candidateinit(candidate);
	trickle_msg->set_target(target);
	sendRequest(request);
	return;
}

void SignalClient::SendMuteTrack(std::string& track_sid, bool muted) {
	livekit::SignalRequest request;
	auto* mute_msg = request.mutable_mute();
	mute_msg->set_sid(track_sid);
	mute_msg->set_muted(muted);
	sendRequest(request);
	return;
}

void SignalClient::SendAddTrack(livekit::AddTrackRequest& request) {
	livekit::SignalRequest req;
	auto* add_track_msg = req.mutable_add_track();
	add_track_msg->CopyFrom(request);
	sendRequest(req);
	return;
}

void SignalClient::SendUpdateLocalMetadata(const std::string& metadata, const std::string& name,
                                           const std::map<std::string, std::string> attributes) {
	const uint64_t request_id = getNextRequestId();
	livekit::SignalRequest req;
	auto* update_msg = req.mutable_update_metadata();
	update_msg->set_request_id(request_id);
	update_msg->set_metadata(metadata);
	update_msg->set_name(name);
	for (const auto& attr : attributes) {
		update_msg->mutable_attributes()->insert({attr.first, attr.second});
	}
	sendRequest(req);
	return;
}

void SignalClient::SendUpdateTrackSettings(const livekit::UpdateTrackSettings& seetings) {
	livekit::SignalRequest req;
	auto* update_msg = req.mutable_track_setting();
	update_msg->CopyFrom(seetings);
	sendRequest(req);
	return;
}

void SignalClient::SendUpdateSubscription(const livekit::UpdateSubscription& subscription) {
	livekit::SignalRequest req;
	auto* update_msg = req.mutable_subscription();
	update_msg->CopyFrom(subscription);
	sendRequest(req);
	return;
}

void SignalClient::SendSyncState(const livekit::SyncState& sync) {
	livekit::SignalRequest req;
	auto* update_msg = req.mutable_sync_state();
	update_msg->CopyFrom(sync);
	sendRequest(req);
	return;
}

void SignalClient::SendUpdateVideoLayers(const std::string& track_sid,
                                         const std::vector<livekit::VideoLayer>& layers) {
	livekit::SignalRequest req;
	auto* update_msg = req.mutable_update_layers();
	update_msg->set_track_sid(track_sid);
	for (const auto& layer : layers) {
		update_msg->mutable_layers()->Add()->CopyFrom(layer);
	}
	sendRequest(req);
	return;
}

void SignalClient::SendUpdateSubscriptionPermissions(
    bool all_participants, const std::vector<livekit::TrackPermission>& track_permissions) {
	livekit::SignalRequest req;
	auto* update_msg = req.mutable_subscription_permission();
	update_msg->set_all_participants(all_participants);
	for (const auto& permission : track_permissions) {
		update_msg->mutable_track_permissions()->Add()->CopyFrom(permission);
	}
	sendRequest(req);
	return;
}

void SignalClient::SendSimulateScenario(const livekit::SimulateScenario& scenario) {
	livekit::SignalRequest req;
	auto* update_msg = req.mutable_simulate();
	update_msg->CopyFrom(scenario);
	sendRequest(req);
	return;
}

void SignalClient::SendPing() {
	livekit::SignalRequest req;
	req.set_ping(utils::GetCurrentTimeMs());
	sendRequest(req);

	livekit::SignalRequest ping_req;
	auto* ping_msg = ping_req.mutable_ping_req();
	ping_msg->set_rtt(this->rtt());
	ping_msg->set_timestamp(utils::GetCurrentTimeMs());
	sendRequest(ping_req);
	return;
}

void SignalClient::SendUpdateLocalAudioTrack(
    const std::string& track_sid, const std::vector<livekit::AudioTrackFeature>& features) {
	livekit::SignalRequest req;
	auto* update_msg = req.mutable_update_audio_track();
	update_msg->set_track_sid(track_sid);
	for (const auto& feature : features) {
		update_msg->add_features(feature);
	}
	sendRequest(req);
	return;
}

void SignalClient::onWsMessage(std::shared_ptr<WebsocketData>& data) {
	if (data->type == WebsocketDataType::Binany) {
		std::cout << "WebSocket binany message, len:" << data->length << std::endl;
		handleWsBinanyMessage(data);
	} else {
		std::cout << "WebSocket message, len:" << data->length
		          << ", data:" << std::string_view((char*)data->data, data->length) << std::endl;
	}

	return;
}

void SignalClient::onWsEvent(enum EventCode code, EventReason reason) {
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

void SignalClient::handleWsBinanyMessage(std::shared_ptr<WebsocketData>& data) {
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
					this->startPingInterval();
				}

				promise_.set_value(resp.join());
				break;
			case livekit::SignalResponse::MessageCase::kLeave:
				if (isEstablishingConnection()) {
					promise_.set_value(livekit::JoinResponse());
				}
				break;
			default:
				if (state_ == SignalConnectionState::RECONNECTING) {
					state_ = SignalConnectionState::CONNECTED;
					this->startPingInterval();
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

		this->handleSignalResponse(resp);
	}

	return;
}

bool SignalClient::isEstablishingConnection() {
	return (state_ == SignalConnectionState::CONNECTING ||
	        state_ == SignalConnectionState::RECONNECTING);
}

void SignalClient::handleSignalResponse(livekit::SignalResponse& resp) {
	bool ping_handled = false;
	switch (resp.message_case()) {
	case livekit::SignalResponse::MessageCase::kAnswer: {
		std::unique_ptr<webrtc::SessionDescriptionInterface> sd = nullptr;
		try {
			sd = fromProtoSessionDescription(resp.answer());
		} catch (const std::exception& e) {
			std::cerr << e.what() << '\n';
		}
		if (sd == nullptr) {
			std::cout << "failed to parse answer" << std::endl;
			return;
		}
		if (observer_) {
			observer_->OnAnswer(std::move(sd));
		}
		break;
	}
	case livekit::SignalResponse::MessageCase::kOffer: {
		std::unique_ptr<webrtc::SessionDescriptionInterface> sd = nullptr;
		try {
			sd = fromProtoSessionDescription(resp.answer());
		} catch (const std::exception& e) {
			std::cerr << e.what() << '\n';
		}
		if (sd == nullptr) {
			std::cout << "failed to parse answer" << std::endl;
			return;
		}
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
		this->rtt_.store(now - lastPingTimestamp);
		this->resetPingTimeout();
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
		this->resetPingTimeout();
	}
}

void SignalClient::handleOnClose(std::string reason) {
	std::cout << "WebSocket closed, reason:" << reason << std::endl;
	return;
}

void SignalClient::resetPingTimeout() {
	this->clearPingTimeout();
	if (!this->ping_timeout_duration_) {
		std::cout << "ping timeout duration is 0, skip reset ping timeout" << std::endl;
		return;
	}

	pingTimeoutTimer_ = std::make_shared<Timer>();
	pingTimeoutTimer_->SetTimeout(
	    [this]() {
		    std::cout << "ping timeout" << std::endl;
		    this->handleOnClose("ping timeout");
	    },
	    this->ping_timeout_duration_ * 1000);

	return;
}

void SignalClient::clearPingTimeout() {
	if (pingTimeoutTimer_) {
		pingTimeoutTimer_->Stop();
	}
	return;
}

void SignalClient::startPingInterval() {
	this->clearPingInterval();
	this->resetPingTimeout();
	if (!this->ping_interval_duration_) {
		std::cout << "ping interval duration is 0, skip start ping interval" << std::endl;
		return;
	}

	pingIntervalTimer_ = std::make_shared<Timer>();
	pingIntervalTimer_->SetInterval(
	    [this]() {
		    std::cout << "ping interval" << std::endl;
		    this->SendPing();
	    },
	    this->ping_interval_duration_ * 1000);

	return;
}

void SignalClient::clearPingInterval() {
	if (pingIntervalTimer_) {
		pingIntervalTimer_->Stop();
	}
	return;
}

void SignalClient::sendRequest(livekit::SignalRequest& request, bool from_queue) {
	std::string serialized_request;
	request.SerializeToString(&serialized_request);
	auto ws_data = std::make_unique<WebsocketData>(
	    serialized_request.c_str(), serialized_request.length(), WebsocketDataType::Binany);
	wsc_->send(std::move(ws_data));
	return;
}

uint64_t SignalClient::getNextRequestId() {
	request_id_++;
	return request_id_;
}

int64_t SignalClient::rtt() const { return rtt_.load(); }

std::unique_ptr<SignalClient> SignalClient::Create(std::string url, std::string token,
                                                   SignalOptions option) {
	auto signal_client = std::make_unique<SignalClient>(url, token, option);
	if (!signal_client->init()) {
		return nullptr;
	}
	return signal_client;
}

} // namespace core
} // namespace livekit
