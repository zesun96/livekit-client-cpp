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

namespace {
constexpr auto kAddTrackTimeout = std::chrono::seconds(10);
}

namespace livekit {
namespace core {

RtcEngine::RtcEngine() : is_subscriber_primary_(true) {}

RtcEngine::~RtcEngine() {
	std::cout << "RtcEngine::~RtcEngine()" << std::endl;
	room_listener_.store(nullptr);
	Disconnect();
}

livekit::JoinResponse RtcEngine::Connect(std::string url, std::string token,
                                         EngineOptions options) {
	Disconnect();

	signal_client_ = SignalClient::Create(url, token, options.signal_options);
	signal_client_->AddObserver(this);

	livekit::JoinResponse response = signal_client_->Connect();
	PLOG_DEBUG << "received JoinResponse: " << response.room().name();
	if (!response.has_room()) {
		Disconnect();
		return response;
	}

	std::lock_guard<std::mutex> guard(session_lock_);
	join_resp_ = response;
	is_subscriber_primary_ = response.subscriber_primary();
	rtc_session_ = RtcSession::Create(response, options);
	if (!rtc_session_) {
		join_resp_.Clear();
		return livekit::JoinResponse();
	}
	rtc_session_->AddObserver(this);
	if (!is_subscriber_primary_ || response.fast_publish()) {
		initial_negotiation_thread_ = std::thread([this]() { negotiate(); });
	}

	return response;
}

void RtcEngine::Disconnect() {
	// Signal callbacks run on the WebSocket service thread. Detach and stop that thread before
	// releasing the RTC session it may call into.
	if (signal_client_) {
		signal_client_->RemoveObserver();
		signal_client_->SendLeave();
		signal_client_->Close();
	}

	if (initial_negotiation_thread_.joinable()) {
		initial_negotiation_thread_.join();
	}

	{
		std::lock_guard<std::mutex> guard(session_lock_);
		lossyDC_ = nullptr;
		reliableDC_ = nullptr;
		if (rtc_session_) {
			rtc_session_->RemoveObserver();
			rtc_session_.reset();
		}
		join_resp_.Clear();
	}

	signal_client_.reset();
	std::lock_guard<std::mutex> guard(pending_track_resolvers_lock_);
	pending_track_resolvers_.clear();
}

void RtcEngine::SetRoomObserver(RtcEngineListener* listener) { room_listener_.store(listener); }

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
	if (req.cid().empty()) {
		throw std::runtime_error("cid is empty");
	}
	if (!signal_client_) {
		throw std::runtime_error("signal client is not connected");
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
		auto status = future.wait_for(kAddTrackTimeout);
		if (status == std::future_status::timeout) {
			std::lock_guard<std::mutex> guard(pending_track_resolvers_lock_);
			pending_track_resolvers_.erase(req.cid());
			return std::nullopt;
		}
		return future.get();
	} catch (...) {
		std::lock_guard<std::mutex> guard(pending_track_resolvers_lock_);
		pending_track_resolvers_.erase(req.cid());
		throw;
	}
}

webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>
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
	if (auto* listener = room_listener_.load()) {
		listener->ParticipantUpdateEvent(updates);
	}
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
		if (auto* listener = room_listener_.load()) {
			listener->ConnectedEvent(this->join_resp_);
		}
	}
}

void RtcEngine::OnSignalingChange(PeerTransport::Target target,
                                  webrtc::PeerConnectionInterface::SignalingState newState) {}

void RtcEngine::OnConnectionChange(PeerTransport::Target target,
                                   webrtc::PeerConnectionInterface::PeerConnectionState new_state) {
}

void RtcEngine::OnAddStream(PeerTransport::Target target,
                            webrtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {}

void RtcEngine::OnRemoveStream(PeerTransport::Target target,
                               webrtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {}

void RtcEngine::OnDataChannel(PeerTransport::Target target,
                              webrtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel) {}

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
                                       const std::vector<webrtc::Candidate>& candidates) {}

void RtcEngine::OnIceConnectionReceivingChange(PeerTransport::Target target, bool receiving) {}

void RtcEngine::OnIceCandidateError(PeerTransport::Target target, const std::string& address,
                                    int port, const std::string& url, int error_code,
                                    const std::string& error_text) {}

void RtcEngine::OnAddTrack(
    PeerTransport::Target target, webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
    const std::vector<webrtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) {}

void RtcEngine::OnTrack(PeerTransport::Target target,
                        webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {}

void RtcEngine::OnRemoveTrack(PeerTransport::Target target,
                              webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) {}

void RtcEngine::OnInterestingUsage(PeerTransport::Target target, int usagePattern) {}

void RtcEngine::negotiate() {
	std::lock_guard<std::mutex> guard(session_lock_);
	if (!rtc_session_) {
		return;
	}
	// don't negotiate without any transceivers or data channel,
	// it will generate sdp without ice frag then negotiate failed
	if (rtc_session_->GetPublishTransceiverCount() == 0 && !lossyDC_ && !reliableDC_) {
		this->createDataChannels();
	}
	rtc_session_->Negotiate();
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
