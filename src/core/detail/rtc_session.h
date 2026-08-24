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

#include "livekit/core/option/option.h"
#include "livekit/core/option/rtc_engine_option.h"
#include "livekit_rtc.pb.h"
#include "peer_transport.h"
#include "peer_transport_factory.h"

#include "../track/local_track.h"

#include <atomic>
#include <mutex>

namespace livekit {
namespace core {

class RtcSession : public PeerTransport::PeerTransportListener {

public:
	enum class State { kNew, kConnecting, kConnected, kClosing, kClosed, kFailed };

	class RtcSessionListener {
	public:
		virtual ~RtcSessionListener() = default;
		virtual void OnLocalOffer(PeerTransport::Target target,
		                          std::unique_ptr<webrtc::SessionDescriptionInterface> offer) = 0;

		virtual void
		OnStateChange(State connection_state,
		              webrtc::PeerConnectionInterface::PeerConnectionState pub_state,
		              webrtc::PeerConnectionInterface::PeerConnectionState sub_state) = 0;

		virtual void
		OnSignalingChange(PeerTransport::Target target,
		                  webrtc::PeerConnectionInterface::SignalingState newState) = 0;
		virtual void
		OnConnectionChange(PeerTransport::Target target,
		                   webrtc::PeerConnectionInterface::PeerConnectionState new_state) = 0;
		virtual void OnAddStream(PeerTransport::Target target,
		                         webrtc::scoped_refptr<webrtc::MediaStreamInterface> stream) = 0;
		virtual void OnRemoveStream(PeerTransport::Target target,
		                            webrtc::scoped_refptr<webrtc::MediaStreamInterface> stream) = 0;
		virtual void
		OnDataChannel(PeerTransport::Target target,
		              webrtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel) = 0;
		virtual void OnRenegotiationNeeded(PeerTransport::Target target) = 0;
		virtual void
		OnIceConnectionChange(PeerTransport::Target target,
		                      webrtc::PeerConnectionInterface::IceConnectionState newState) = 0;
		virtual void
		OnIceGatheringChange(PeerTransport::Target target,
		                     webrtc::PeerConnectionInterface::IceGatheringState newState) = 0;
		virtual void OnIceCandidate(PeerTransport::Target target,
		                            const webrtc::IceCandidateInterface* candidate) = 0;
		virtual void OnIceCandidatesRemoved(PeerTransport::Target target,
		                                    const std::vector<webrtc::Candidate>& candidates) = 0;
		virtual void OnIceConnectionReceivingChange(PeerTransport::Target target,
		                                            bool receiving) = 0;
		virtual void OnIceCandidateError(PeerTransport::Target target, const std::string& address,
		                                 int port, const std::string& url, int error_code,
		                                 const std::string& error_text) = 0;
		virtual void OnAddTrack(
		    PeerTransport::Target target,
		    webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
		    const std::vector<webrtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) = 0;
		virtual void OnTrack(PeerTransport::Target target,
		                     webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver,
		                     std::function<std::string()> stats_provider) = 0;
		virtual void
		OnRemoveTrack(PeerTransport::Target target,
		              webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) = 0;
		virtual void OnInterestingUsage(PeerTransport::Target target, int usagePattern) = 0;
	};

public:
	RtcSession(livekit::JoinResponse join_response, EngineOptions options,
	           std::shared_ptr<PeerTransportFactory> peer_factory);
	virtual ~RtcSession();

	static std::unique_ptr<RtcSession> Create(livekit::JoinResponse join_response,
	                                          EngineOptions options,
	                                          std::shared_ptr<PeerTransportFactory> peer_factory);

	bool Init();

	std::shared_ptr<PeerTransportFactory> GetPeerTransportFactory() { return peer_factory_; }

	void AddObserver(RtcSession::RtcSessionListener* observer);
	void RemoveObserver();

	webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>
	CreateSender(LocalTrack* track, TrackPublishOptions options,
	             std::vector<webrtc::RtpEncodingParameters> send_encodings);
	webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>
	CreateSender(webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface> media_track,
	             TrackKind kind, TrackPublishOptions options,
	             std::vector<webrtc::RtpEncodingParameters> send_encodings);
	bool SupportsVideoCodec(VideoCodec codec) const;
	bool RemoveSender(LocalTrack* track);
	bool RemoveSender(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver);
	std::function<std::string()> CreatePublisherStatsProvider(
	    webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) const;
	std::function<std::string()> CreateSubscriberStatsProvider(
	    webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) const;

	void PublisherNegotiationNeeded();

public:
	void SetPublisherAnswer(std::unique_ptr<webrtc::SessionDescriptionInterface> answer);
	std::unique_ptr<webrtc::SessionDescriptionInterface>
	CreateSubscriberAnswerFromOffer(std::unique_ptr<webrtc::SessionDescriptionInterface> offer);
	void AddIceCandidate(const std::string& candidate, const livekit::SignalTarget target);
	bool Negotiate();
	bool ShouldRestartPublisherIce() const;
	bool RestartPublisherIce();
	bool IsConnected() const;
	bool UpdateConfiguration(const livekit::ReconnectResponse& response);
	void PopulateSyncState(livekit::SyncState& sync);
	webrtc::scoped_refptr<webrtc::DataChannelInterface>
	CreateDataChannel(const std::string& label, const webrtc::DataChannelInit* dataChannelDict);

	const std::size_t GetPublishTransceiverCount() const;

	void update_state();

public:
	/* Pure virtual methods inherited from PeerTransport::PeerTransportListener. */
	virtual void OnOffer(PeerTransport::Target target,
	                     std::unique_ptr<webrtc::SessionDescriptionInterface> offer) override;
	virtual void
	OnSignalingChange(PeerTransport::Target target,
	                  webrtc::PeerConnectionInterface::SignalingState newState) override;
	virtual void
	OnConnectionChange(PeerTransport::Target target,
	                   webrtc::PeerConnectionInterface::PeerConnectionState new_state) override;
	virtual void OnAddStream(PeerTransport::Target target,
	                         webrtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;
	virtual void
	OnRemoveStream(PeerTransport::Target target,
	               webrtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;
	virtual void
	OnDataChannel(PeerTransport::Target target,
	              webrtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel) override;
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
	                                    const std::vector<webrtc::Candidate>& candidates) override;
	virtual void OnIceConnectionReceivingChange(PeerTransport::Target target,
	                                            bool receiving) override;
	virtual void OnIceCandidateError(PeerTransport::Target target, const std::string& address,
	                                 int port, const std::string& url, int error_code,
	                                 const std::string& error_text) override;
	virtual void OnAddTrack(
	    PeerTransport::Target target, webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
	    const std::vector<webrtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) override;
	virtual void
	OnTrack(PeerTransport::Target target,
	        webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;
	virtual void
	OnRemoveTrack(PeerTransport::Target target,
	              webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override;
	virtual void OnInterestingUsage(PeerTransport::Target target, int usagePattern) override;

private:
	bool RequestPublisherNegotiation(bool ice_restart);
	void PublisherNegotiationCompleted();

	std::shared_ptr<PeerTransportFactory> peer_factory_;
	livekit::JoinResponse join_response_;
	RtcSessionListener* observer_ = nullptr;
	std::unique_ptr<PeerTransport> publisher_pc_;
	std::unique_ptr<PeerTransport> subscriber_pc_;
	EngineOptions options_;
	std::atomic<bool> is_publisher_connection_required_;
	std::atomic<bool> is_subscriber_connection_required_;
	std::atomic<State> state_ = State::kNew;
	std::atomic<bool> has_published_ = false;
	std::mutex publisher_negotiation_mutex_;
	bool publisher_negotiation_in_flight_ = false;
	bool publisher_negotiation_pending_ = false;
	bool publisher_ice_restart_pending_ = false;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_RTC_SESSION_H_
