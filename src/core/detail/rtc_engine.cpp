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
#include <nlohmann/json.hpp>

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
	join_resp_ = response;
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

void RtcEngine::SetRoomObserver(RtcEngineListener* listener) { room_listener_ = listener; }

void RtcEngine::OnAnswer(std::unique_ptr<webrtc::SessionDescriptionInterface> answer) {
	std::lock_guard<std::mutex> guard(session_lock_);
	if (rtc_session_) {
		rtc_session_->SetPublisherAnswer(std::move(answer));
	}
	return;
}

std::shared_ptr<PeerTransportFactory> RtcEngine::GetSessionPeerTransportFactory() {
	std::lock_guard<std::mutex> guard(session_lock_);
	if (rtc_session_) {
		return rtc_session_->GetPeerTransportFactory();
	}
	return nullptr;
}

std::optional<livekit::TrackInfo> RtcEngine::AddTrack(const livekit::AddTrackRequest& req) {
	if (req.cid() == "") {
		throw std::runtime_error("cid is empty");
	}

	std::promise<livekit::TrackInfo> promise;
	std::future<livekit::TrackInfo> future = promise.get_future();
	{
		std::lock_guard<std::mutex> guard(pending_track_resolvers_lock_);
		if (pending_track_resolvers_.count(req.cid()) > 0) {
			throw std::runtime_error("a track with the same ID has already been published");
		}
		pending_track_resolvers_[req.cid()] = std::move(promise);
	}

	try {
		signal_client_->SendAddTrack(req);
		auto status = future.wait_for(std::chrono::milliseconds(150+200));
		if (status == std::future_status::timeout) {
			return std::nullopt;
		}
		return future.get();
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		throw e;
	}
	return std::nullopt;
}

rtc::scoped_refptr<webrtc::RtpTransceiverInterface>
RtcEngine::CreateSender(LocalTrack* track, TrackPublishOptions options,
                        std::vector<webrtc::RtpEncodingParameters> send_encodings) {
	std::lock_guard<std::mutex> guard(session_lock_);
	if (rtc_session_) {
		return rtc_session_->CreateSender(track, options, send_encodings);
	}
	return nullptr;
}

void RtcEngine::PublisherNegotiationNeeded() {
	std::lock_guard<std::mutex> guard(session_lock_);
	if (rtc_session_) {
		return rtc_session_->PublisherNegotiationNeeded();
	}
}

void RtcEngine::OnLeave(const livekit::LeaveRequest leave) { return; }

void RtcEngine::OnLocalTrackPublished(const livekit::TrackPublishedResponse& response) {
	std::cout << "received trackPublishedResponse:" << response.cid() << "; "
	          << response.track().sid() << std::endl;

	auto& cid = response.cid();
	{
		std::lock_guard<std::mutex> guard(pending_track_resolvers_lock_);
		auto it = pending_track_resolvers_.find(cid);
		if (it == pending_track_resolvers_.end()) {
			std::cerr << "missing track resolver for " << cid << std::endl;
			return;
		}
		auto& promise = it->second;
		promise.set_value(response.track());
		pending_track_resolvers_.erase(it);
	}
	return;
}

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

void RtcEngine::OnLocalOffer(PeerTransport::Target target,
                             std::unique_ptr<webrtc::SessionDescriptionInterface> offer) {
	this->signal_client_->SendOffer(std::move(offer));
}

void RtcEngine::OnStateChange(RtcSession::State connection_state,
                              webrtc::PeerConnectionInterface::PeerConnectionState pub_state,
                              webrtc::PeerConnectionInterface::PeerConnectionState sub_state) {
	std::cout << "RtcEngine::OnStateChange()" << int(connection_state) << std::endl;
	if (connection_state == RtcSession::State::kConnected) {
		if (room_listener_) {
			room_listener_->ConnectedEvent(this->join_resp_);
		}
	}
}

void RtcEngine::OnSignalingChange(PeerTransport::Target target,
                                  webrtc::PeerConnectionInterface::SignalingState newState) {}

void RtcEngine::OnConnectionChange(PeerTransport::Target target,
                                   webrtc::PeerConnectionInterface::PeerConnectionState new_state) {
}

void RtcEngine::OnAddStream(PeerTransport::Target target,
                            rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {}

void RtcEngine::OnRemoveStream(PeerTransport::Target target,
                               rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {}

void RtcEngine::OnDataChannel(PeerTransport::Target target,
                              rtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel) {}

void RtcEngine::OnRenegotiationNeeded(PeerTransport::Target target) {}

void RtcEngine::OnIceConnectionChange(
    PeerTransport::Target target, webrtc::PeerConnectionInterface::IceConnectionState newState) {}

void RtcEngine::OnIceGatheringChange(PeerTransport::Target target,
                                     webrtc::PeerConnectionInterface::IceGatheringState newState) {}

void RtcEngine::OnIceCandidate(PeerTransport::Target target,
                               const webrtc::IceCandidateInterface* candidate) {

	std::string candidate_str;
	candidate->ToString(&candidate_str);

	nlohmann::json candidate_json;
	candidate_json["candidate"] = candidate_str;
	candidate_json["sdpMid"] = candidate->sdp_mid();
	candidate_json["sdpMLineIndex"] = candidate->sdp_mline_index();

	auto candidate_json_str = candidate_json.dump();

	if (target == PeerTransport::Target::PUBLISHER) {
		signal_client_->SendIceCandidate(candidate_json_str, livekit::SignalTarget::PUBLISHER);
	} else if (target == PeerTransport::Target::SUBSCRIBER) {
		signal_client_->SendIceCandidate(candidate_json_str, livekit::SignalTarget::SUBSCRIBER);
	}

	return;
}

void RtcEngine::OnIceCandidatesRemoved(PeerTransport::Target target,
                                       const std::vector<cricket::Candidate>& candidates) {}

void RtcEngine::OnIceConnectionReceivingChange(PeerTransport::Target target, bool receiving) {}

void RtcEngine::OnIceCandidateError(PeerTransport::Target target, const std::string& address,
                                    int port, const std::string& url, int error_code,
                                    const std::string& error_text) {}

void RtcEngine::OnAddTrack(
    PeerTransport::Target target, rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
    const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) {}

void RtcEngine::OnTrack(PeerTransport::Target target,
                        rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {}

void RtcEngine::OnRemoveTrack(PeerTransport::Target target,
                              rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) {}

void RtcEngine::OnInterestingUsage(PeerTransport::Target target, int usagePattern) {}

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
