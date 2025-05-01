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

#include "rtc_session.h"

#include "api/peer_connection_interface.h"

namespace {

static webrtc::PeerConnectionInterface::RTCConfiguration
make_rtc_config_join(livekit::JoinResponse join_response, livekit::core::EngineOptions options) {
	webrtc::PeerConnectionInterface::RTCConfiguration rtc_config;

	// Add ICE servers from options
	for (auto& ice_server : options.rtc_config.ice_servers) {
		if (ice_server.urls.empty()) {
			continue;
		}
		webrtc::PeerConnectionInterface::IceServer rtc_ice_server;
		rtc_ice_server.username = ice_server.username;
		rtc_ice_server.password = ice_server.password;
		for (auto url : ice_server.urls) {
			rtc_ice_server.urls.emplace_back(url.c_str());
		}
		rtc_config.servers.push_back(rtc_ice_server);
	}

	// Add ICE servers from join_response
	for (auto& ice_server : join_response.ice_servers()) {
		if (ice_server.urls().empty()) {
			continue;
		}
		webrtc::PeerConnectionInterface::IceServer rtc_ice_server;
		rtc_ice_server.username = ice_server.username();
		rtc_ice_server.password = ice_server.credential();
		for (auto& url : ice_server.urls()) {
			rtc_ice_server.urls.emplace_back(url.c_str());
		}
		rtc_config.servers.push_back(rtc_ice_server);
	}

	// Set continual gathering policy
	rtc_config.continual_gathering_policy =
	    static_cast<webrtc::PeerConnectionInterface::ContinualGatheringPolicy>(
	        options.rtc_config.continual_gathering_policy);

	// Set ICE transport type
	rtc_config.type = static_cast<webrtc::PeerConnectionInterface::IceTransportsType>(
	    options.rtc_config.ice_transport_type);
	if (join_response.has_client_configuration()) {
		auto& client_config = join_response.client_configuration();
		if (client_config.force_relay()) {
			rtc_config.type = webrtc::PeerConnectionInterface::IceTransportsType::kRelay;
		}
	}

	rtc_config.ice_candidate_pool_size = 5;

	rtc_config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;

	return rtc_config;
}

} // namespace

namespace livekit {
namespace core {

RtcSession::RtcSession(livekit::JoinResponse join_response, EngineOptions options)
    : join_response_(join_response), options_(options) {

	bool subscriber_primary = join_response.subscriber_primary();
	is_publisher_connection_required_ = !subscriber_primary;
	is_subscriber_connection_required_ = subscriber_primary;
}

RtcSession::~RtcSession() { std::cout << "RtcSession::~RtcSession()" << std::endl; }

bool RtcSession::Init() {
	auto rtc_config = make_rtc_config_join(join_response_, options_);
	publisher_pc_ =
	    std::make_unique<PeerTransport>(PeerTransport::Target::PUBLISHER, rtc_config, nullptr);
	this->publisher_pc_->AddPeerTransportListener(this);
	if (!publisher_pc_->Init()) {
		return false;
	}

	subscriber_pc_ =
	    std::make_unique<PeerTransport>(PeerTransport::Target::SUBSCRIBER, rtc_config, nullptr);
	this->subscriber_pc_->AddPeerTransportListener(this);
	if (!subscriber_pc_->Init()) {
		return false;
	}

	return true;
}

void RtcSession::AddObserver(RtcSession::RtcSessionListener* observer) {
	this->observer_ = observer;
}

void RtcSession::RemoveObserver() { this->observer_ = nullptr; }

void RtcSession::SetPublisherAnswer(std::unique_ptr<webrtc::SessionDescriptionInterface> answer) {
	this->publisher_pc_->SetRemoteDescription(std::move(answer));
	return;
}

std::unique_ptr<webrtc::SessionDescriptionInterface> RtcSession::CreateSubscriberAnswerFromOffer(
    std::unique_ptr<webrtc::SessionDescriptionInterface> offer) {
	std::string str_desc;
	offer->ToString(&str_desc);
	std::cout << "recived offer: " << str_desc << std::endl;

	subscriber_pc_->SetRemoteDescription(std::move(offer));

	webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
	options.offer_to_receive_audio = true;
	options.offer_to_receive_video = true;
	options.use_rtp_mux = true;
	options.use_obsolete_sctp_sdp = true;
	options.ice_restart = true;
	auto answer = subscriber_pc_->CreateAnswer(options);

	std::unique_ptr<webrtc::SessionDescriptionInterface> answer_desc =
	    ConvertSdp(webrtc::SdpType::kAnswer, answer);

	subscriber_pc_->SetLocalDescription(std::move(answer_desc));

	std::unique_ptr<webrtc::SessionDescriptionInterface> answer_new_desc =
	    ConvertSdp(webrtc::SdpType::kAnswer, answer);
	return answer_new_desc;
}

void RtcSession::AddIceCandidate(const std::string& candidate, const livekit::SignalTarget target) {
	if (target == livekit::SignalTarget::PUBLISHER) {
		publisher_pc_->AddIceCandidate(candidate);
	} else {
		subscriber_pc_->AddIceCandidate(candidate);
	}
	return;
}

bool RtcSession::Negotiate() { return this->publisher_pc_->Negotiate(); }

rtc::scoped_refptr<webrtc::DataChannelInterface>
RtcSession::CreateDataChannel(const std::string& label,
                              const webrtc::DataChannelInit* dataChannelDict) {
	return this->publisher_pc_->CreateDataChannel(label, dataChannelDict);
}

const std::size_t RtcSession::GetPublishTransceiverCount() const {
	return this->publisher_pc_->GetTransceivers().size();
}

std::unique_ptr<RtcSession> RtcSession::Create(livekit::JoinResponse join_response,
                                               EngineOptions options) {
	auto rtc_session = std::make_unique<RtcSession>(join_response, options);
	if (!rtc_session->Init()) {
		return nullptr;
	}
	return std::move(rtc_session);
}

void RtcSession::OnOffer(PeerTransport::Target target,
                         std::unique_ptr<webrtc::SessionDescriptionInterface> offer) {
	if (this->observer_) {
		this->observer_->OnLocalOffer(target, std::move(offer));
	}
	return;
}

void RtcSession::OnSignalingChange(PeerTransport::Target target,
                                   webrtc::PeerConnectionInterface::SignalingState newState) {}

void RtcSession::OnConnectionChange(
    PeerTransport::Target target, webrtc::PeerConnectionInterface::PeerConnectionState new_state) {}

void RtcSession::OnAddStream(PeerTransport::Target target,
                             rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {}

void RtcSession::OnRemoveStream(PeerTransport::Target target,
                                rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {}

void RtcSession::OnDataChannel(PeerTransport::Target target,
                               rtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel) {}

void RtcSession::OnRenegotiationNeeded(PeerTransport::Target target) {}

void RtcSession::OnIceConnectionChange(
    PeerTransport::Target target, webrtc::PeerConnectionInterface::IceConnectionState newState) {}

void RtcSession::OnIceGatheringChange(PeerTransport::Target target,
                                      webrtc::PeerConnectionInterface::IceGatheringState newState) {
}

void RtcSession::OnIceCandidate(PeerTransport::Target target,
                                const webrtc::IceCandidateInterface* candidate) {

	if (this->observer_) {
		this->observer_->OnIceCandidate(target, candidate);
	}
	return;
}

void RtcSession::OnIceCandidatesRemoved(PeerTransport::Target target,
                                        const std::vector<cricket::Candidate>& candidates) {}

void RtcSession::OnIceConnectionReceivingChange(PeerTransport::Target target, bool receiving) {}

void RtcSession::OnIceCandidateError(PeerTransport::Target target, const std::string& address,
                                     int port, const std::string& url, int error_code,
                                     const std::string& error_text) {}

void RtcSession::OnAddTrack(
    PeerTransport::Target target, rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
    const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) {}

void RtcSession::OnTrack(PeerTransport::Target target,
                         rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {}

void RtcSession::OnRemoveTrack(PeerTransport::Target target,
                               rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) {}

void RtcSession::OnInterestingUsage(PeerTransport::Target target, int usagePattern) {}

} // namespace core
} // namespace livekit
