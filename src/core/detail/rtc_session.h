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

#ifndef _LKC_CORE_DETAIL_RTC_SESSION_H_
#define _LKC_CORE_DETAIL_RTC_SESSION_H_

#include "livekit/core/option/rtc_engine_option.h"
#include "livekit_rtc.pb.h"
#include "peer_transport.h"

namespace livekit {
namespace core {
class RtcSession : public PeerTransport::PrivateListener,
                   public PeerTransport::PeerTransportListener {

public:
	class RtcSessionListener {
	public:
		virtual ~RtcSessionListener() = default;
		virtual void OnLocalOffer(std::unique_ptr<webrtc::SessionDescriptionInterface> offer) = 0;
	};

public:
	RtcSession(livekit::JoinResponse join_response, EngineOptions options);
	virtual ~RtcSession();

	static std::unique_ptr<RtcSession> Create(livekit::JoinResponse join_response,
	                                          EngineOptions options);

	bool Init();

	void AddObserver(RtcSession::RtcSessionListener* observer);
	void RemoveObserver();

public:
	void SetPublisherAnswer(std::unique_ptr<webrtc::SessionDescriptionInterface> answer);
	std::unique_ptr<webrtc::SessionDescriptionInterface>
	CreateSubscriberAnswerFromOffer(std::unique_ptr<webrtc::SessionDescriptionInterface> offer);
	void AddIceCandidate(const std::string& candidate, const livekit::SignalTarget target);
	bool Negotiate();

private:
	void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;
	void OnIceCandidateError(const std::string& address, int port, const std::string& url,
	                         int error_code, const std::string& error_text) override;
	void
	OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState new_state) override;
	void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel) override;
	void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;

	void OnOffer(std::unique_ptr<webrtc::SessionDescriptionInterface> offer);

private:
	livekit::JoinResponse join_response_;
	RtcSessionListener* observer_ = nullptr;
	std::unique_ptr<PeerTransport> publisher_pc_;
	std::unique_ptr<PeerTransport> subscriber_pc_;
	EngineOptions options_;
	bool is_publisher_connection_required_;
	bool is_subscriber_connection_required_;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_RTC_SESSION_H_
