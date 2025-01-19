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

	for (auto& ice_server : options.rtc_config.ice_servers) {
		webrtc::PeerConnectionInterface::IceServer rtc_ice_server;
		rtc_ice_server.username = ice_server.username.c_str();
		rtc_ice_server.password = ice_server.password.c_str();
		for (auto url : ice_server.urls) {
			rtc_ice_server.urls.emplace_back(url.c_str());
		}
		rtc_config.servers.push_back(rtc_ice_server);
	}

	for (auto& ice_server : join_response.ice_servers()) {
		webrtc::PeerConnectionInterface::IceServer rtc_ice_server;
		rtc_ice_server.username = ice_server.username().c_str();
		rtc_ice_server.password = ice_server.credential().c_str();
		for (auto url : ice_server.urls()) {
			rtc_ice_server.urls.emplace_back(url.c_str());
		}
		rtc_config.servers.push_back(rtc_ice_server);
	}

	rtc_config.continual_gathering_policy =
	    static_cast<webrtc::PeerConnectionInterface::ContinualGatheringPolicy>(
	        options.rtc_config.continual_gathering_policy);

	rtc_config.type = static_cast<webrtc::PeerConnectionInterface::IceTransportsType>(
	    options.rtc_config.ice_transport_type);

	return rtc_config;
}

} // namespace

namespace livekit {
namespace core {

RtcSession::RtcSession(livekit::JoinResponse join_response, EngineOptions options)
    : join_response_(join_response), options_(options) {}

RtcSession::~RtcSession() {}

bool RtcSession::Init() {
	webrtc::PeerConnectionInterface::RTCConfiguration rtc_config =
	    make_rtc_config_join(join_response_, options_);
	publisher_pc_ = std::make_unique<PeerTransport>(rtc_config, nullptr);
	if (publisher_pc_->Init(this)) {
		return false;
	}
	subscriber_pc_ = std::make_unique<PeerTransport>(rtc_config, nullptr);
	if (subscriber_pc_->Init(this)) {
		return false;
	}
	return true;
}

std::unique_ptr<RtcSession> RtcSession::Create(livekit::JoinResponse join_response,
                                               EngineOptions options) {
	auto rtc_session = std::make_unique<RtcSession>(join_response, options);
	if (!rtc_session->Init()) {
		return nullptr;
	}
	return rtc_session;
}

void RtcSession::OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {}

void RtcSession::OnIceCandidateError(const std::string& address, int port, const std::string& url,
                                     int error_code, const std::string& error_text) {}

void RtcSession::OnConnectionChange(
    webrtc::PeerConnectionInterface::PeerConnectionState new_state) {}

void RtcSession::OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel) {}

void RtcSession::OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {}

} // namespace core
} // namespace livekit
