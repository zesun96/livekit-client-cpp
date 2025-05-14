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

#ifndef _LKC_CORE_DETAIL_RTC_ENGINE_H_
#define _LKC_CORE_DETAIL_RTC_ENGINE_H_

#include "livekit/core/option/rtc_engine_option.h"
#include "livekit_rtc.pb.h"
#include "rtc_session.h"
#include "signal_client.h"

#include <memory>
#include <mutex>
#include <string>

namespace livekit {
namespace core {

class RtcSession;

class SignalClient;
class RtcEngine : public SignalClientObserver, public RtcSession::RtcSessionListener {
public:
	RtcEngine();
	~RtcEngine();

	livekit::JoinResponse Connect(std::string url, std::string token, EngineOptions options);

	/* Pure virtual methods inherited from SignalClientObserver */
public:
	virtual void OnAnswer(std::unique_ptr<webrtc::SessionDescriptionInterface> answer) override;
	virtual void OnLeave(const livekit::LeaveRequest leave) override;
	virtual void OnLocalTrackPublished(const livekit::TrackPublishedResponse& response) override;
	virtual void
	OnLocalTrackUnpublished(const livekit::TrackUnpublishedResponse& response) override;
	virtual void OnOffer(std::unique_ptr<webrtc::SessionDescriptionInterface> offer) override;
	virtual void OnRemoteMuteChanged(std::string sid, bool muted) override;
	virtual void OnSubscribedQualityUpdate(const livekit::SubscribedQualityUpdate& update) override;
	virtual void OnTokenRefresh(const std::string& token) override;
	virtual void OnTrickle(std::string& candidate, livekit::SignalTarget target) override;
	virtual void OnClose() override;
	virtual void OnParticipantUpdate(const std::vector<livekit::ParticipantInfo>& updates) override;
	virtual void OnSpeakersChanged(std::vector<livekit::SpeakerInfo>& update) override;
	virtual void OnRoomUpdate(const livekit::Room& update) override;
	virtual void
	OnConnectionQuality(const std::vector<livekit::ConnectionQualityInfo>& update) override;
	virtual void OnStreamStateUpdate(const std::vector<livekit::StreamStateInfo>& update) override;
	virtual void
	OnSubscriptionPermissionUpdate(const livekit::SubscriptionPermissionUpdate& update) override;
	virtual void OnSubscriptionError(const livekit::SubscriptionResponse& response) override;
	virtual void OnRequestResponse(const livekit::RequestResponse& response) override;
	virtual void OnLocalTrackSubscribed(const std::string& track_sid) override;

	/* Pure virtual methods inherited from RtcSession::RtcSessionListener */
public:
	virtual void OnLocalOffer(PeerTransport::Target target,
	                          std::unique_ptr<webrtc::SessionDescriptionInterface> offer) override;
	virtual void
	OnSignalingChange(PeerTransport::Target target,
	                  webrtc::PeerConnectionInterface::SignalingState newState) override;
	virtual void
	OnConnectionChange(PeerTransport::Target target,
	                   webrtc::PeerConnectionInterface::PeerConnectionState new_state) override;
	virtual void OnAddStream(PeerTransport::Target target,
	                         rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;
	virtual void OnRemoveStream(PeerTransport::Target target,
	                            rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;
	virtual void
	OnDataChannel(PeerTransport::Target target,
	              rtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel) override;
	virtual void OnRenegotiationNeeded(PeerTransport::Target target) override;
	virtual void
	OnIceConnectionChange(PeerTransport::Target target,
	                      webrtc::PeerConnectionInterface::IceConnectionState newState) override;
	virtual void
	OnIceGatheringChange(PeerTransport::Target target,
	                     webrtc::PeerConnectionInterface::IceGatheringState newState) override;
	virtual void OnIceCandidate(PeerTransport::Target target,
	                            const webrtc::IceCandidateInterface* candidate) override;
	virtual void OnIceCandidatesRemoved(PeerTransport::Target target,
	                                    const std::vector<cricket::Candidate>& candidates) override;
	virtual void OnIceConnectionReceivingChange(PeerTransport::Target target,
	                                            bool receiving) override;
	virtual void OnIceCandidateError(PeerTransport::Target target, const std::string& address,
	                                 int port, const std::string& url, int error_code,
	                                 const std::string& error_text) override;
	virtual void OnAddTrack(
	    PeerTransport::Target target, rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
	    const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) override;
	virtual void OnTrack(PeerTransport::Target target,
	                     rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;
	virtual void OnRemoveTrack(PeerTransport::Target target,
	                           rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override;
	virtual void OnInterestingUsage(PeerTransport::Target target, int usagePattern) override;

private:
	void negotiate();
	void createDataChannels();

private:
	mutable std::mutex session_lock_;
	std::unique_ptr<SignalClient> signal_client_;
	std::unique_ptr<RtcSession> rtc_session_;
	bool is_subscriber_primary_;
	rtc::scoped_refptr<webrtc::DataChannelInterface> lossyDC_ = nullptr;
	rtc::scoped_refptr<webrtc::DataChannelInterface> reliableDC_ = nullptr;
};

} // namespace core
} // namespace livekit

#endif //
