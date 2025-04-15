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

#include "rtc_engine.h"
#include "internals.h"
#include "rtc_session.h"
#include "signal_client.h"

#include <future>

namespace livekit {
namespace core {

RtcEngine::RtcEngine() : is_subscriber_primary_(true) {}

RtcEngine::~RtcEngine() {
	std::cout << "RtcEngine::~RtcEngine()" << std::endl;
	if (signal_client_) {
		signal_client_->RemoveObserver();
	}
	return;
}

livekit::JoinResponse RtcEngine::Connect(std::string url, std::string token,
                                         EngineOptions options) {
	signal_client_ = SignalClient::Create(url, token, options.signal_options);
	signal_client_->AddObserver(this);

	std::lock_guard<std::mutex> guard(session_lock_);
	livekit::JoinResponse response = signal_client_->Connect();
	PLOG_DEBUG << "received JoinResponse: " << response.room().name();
	is_subscriber_primary_ = response.subscriber_primary();
	if (response.has_room()) {
		rtc_session_ = RtcSession::Create(response, options);
		this->rtc_session_->AddObserver(this);
		if (!is_subscriber_primary_ || response.fast_publish()) {
			// std::async(std::launch::async, [this]() { return this->negotiate(); });
			std::thread t([this]() { return this->negotiate(); });
			t.detach();
		}
	}

	return response;
}

void RtcEngine::OnAnswer(std::unique_ptr<webrtc::SessionDescriptionInterface> answer) {
	std::lock_guard<std::mutex> guard(session_lock_);
	if (rtc_session_) {
		rtc_session_->SetPublisherAnswer(std::move(answer));
	}
	return;
}

void RtcEngine::OnLeave(const livekit::LeaveRequest leave) { return; }
void RtcEngine::OnLocalTrackPublished(const livekit::TrackPublishedResponse& response) { return; }
void RtcEngine::OnLocalTrackUnpublished(const livekit::TrackUnpublishedResponse& response) {
	return;
}

void RtcEngine::OnOffer(std::unique_ptr<webrtc::SessionDescriptionInterface> offer) {
	std::lock_guard<std::mutex> guard(session_lock_);
	if (rtc_session_) {
		auto answer = rtc_session_->CreateSubscriberAnswerFromOffer(std::move(offer));
		if (answer) {
			this->signal_client_->SendAnswer(std::move(answer));
		}
	}
	return;
}
void RtcEngine::OnRemoteMuteChanged(std::string sid, bool muted) { return; }
void RtcEngine::OnSubscribedQualityUpdate(const livekit::SubscribedQualityUpdate& update) {
	return;
}
void RtcEngine::OnTokenRefresh(const std::string& token) { return; }

void RtcEngine::OnTrickle(std::string& candidate, livekit::SignalTarget target) {
	std::lock_guard<std::mutex> guard(session_lock_);
	if (rtc_session_) {
		rtc_session_->AddIceCandidate(candidate, target);
	}
	return;
}
void RtcEngine::OnClose() { return; }
void RtcEngine::OnParticipantUpdate(const std::vector<livekit::ParticipantInfo>& updates) {
	return;
}
void RtcEngine::OnSpeakersChanged(std::vector<livekit::SpeakerInfo>& update) { return; }
void RtcEngine::OnRoomUpdate(const livekit::Room& update) { return; }
void RtcEngine::OnConnectionQuality(const std::vector<livekit::ConnectionQualityInfo>& update) {
	return;
}
void RtcEngine::OnStreamStateUpdate(const std::vector<livekit::StreamStateInfo>& update) { return; }
void RtcEngine::OnSubscriptionPermissionUpdate(
    const livekit::SubscriptionPermissionUpdate& update) {
	return;
}
void RtcEngine::OnSubscriptionError(const livekit::SubscriptionResponse& response) { return; }
void RtcEngine::OnRequestResponse(const livekit::RequestResponse& response) { return; }
void RtcEngine::OnLocalTrackSubscribed(const std::string& track_sid) { return; }

void RtcEngine::OnLocalOffer(std::unique_ptr<webrtc::SessionDescriptionInterface> offer) {
	this->signal_client_->SendOffer(std::move(offer));
}

void RtcEngine::negotiate() {
	std::lock_guard<std::mutex> guard(session_lock_);
	// don't negotiate without any transceivers or data channel,
	// it will generate sdp without ice frag then negotiate failed
	if (this->rtc_session_ && this->rtc_session_->GetPublishTransceiverCount() == 0 &&
	    !this->lossyDC_ && !this->reliableDC_) {
		this->createDataChannels();
	}
	this->rtc_session_->Negotiate();
}

void RtcEngine::createDataChannels() {
	if (!this->rtc_session_) {
		return;
	}
	struct webrtc::DataChannelInit lossy_init;
	lossy_init.ordered = true;
	lossy_init.reliable = false;
	lossy_init.maxRetransmits = 0;
	this->lossyDC_ = this->rtc_session_->CreateDataChannel("_lossy", &lossy_init);

	struct webrtc::DataChannelInit reliable_init;
	reliable_init.ordered = true;
	reliable_init.reliable = true;
	this->reliableDC_ = this->rtc_session_->CreateDataChannel("_reliable", &reliable_init);
}

} // namespace core
} // namespace livekit
