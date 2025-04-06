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

#include <api/create_peerconnection_factory.h>
#include <api/jsep.h>

#include <future>
#include <memory>
#include <mutex>
#include <string>

namespace livekit {
namespace core {

class JoinRespone;

class SignalClientObserver {
public:
	virtual ~SignalClientObserver() = default;

	virtual void OnAnswer(std::unique_ptr<webrtc::SessionDescriptionInterface> answer) = 0;

	virtual void OnLeave(const livekit::LeaveRequest leave) = 0;

	virtual void OnLocalTrackPublished(const livekit::TrackPublishedResponse& response) = 0;

	virtual void OnLocalTrackUnpublished(const livekit::TrackUnpublishedResponse& response) = 0;

	virtual void OnOffer(std::unique_ptr<webrtc::SessionDescriptionInterface> offer) = 0;

	virtual void OnRemoteMuteChanged(std::string sid, bool muted) = 0;

	virtual void OnSubscribedQualityUpdate(const livekit::SubscribedQualityUpdate& update) = 0;

	virtual void OnTokenRefresh(const std::string& token) = 0;

	virtual void OnTrickle(std::string& candidate, livekit::SignalTarget target) = 0;

	virtual void OnClose() = 0;

	virtual void OnParticipantUpdate(const std::vector<livekit::ParticipantInfo>& updates) = 0;

	virtual void OnSpeakersChanged(std::vector<livekit::SpeakerInfo>& update) = 0;

	virtual void OnRoomUpdate(const livekit::Room& update) = 0;

	virtual void OnConnectionQuality(const std::vector<livekit::ConnectionQualityInfo>& update) = 0;

	virtual void OnStreamStateUpdate(const std::vector<livekit::StreamStateInfo>& update) = 0;

	virtual void
	OnSubscriptionPermissionUpdate(const livekit::SubscriptionPermissionUpdate& update) = 0;

	virtual void OnSubscriptionError(const livekit::SubscriptionResponse& response) = 0;

	virtual void OnRequestResponse(const livekit::RequestResponse& response) = 0;

	virtual void OnLocalTrackSubscribed(const std::string& track_sid) = 0;
};

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

	void AddObserver(SignalClientObserver* observer);
	void RemoveObserver();

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
	SignalClientObserver* observer_ = nullptr;
	int64_t rtt_;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_SIGNAL_CLIENT_H_
