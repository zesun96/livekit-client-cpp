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

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace livekit {
namespace core {

class RtcSession;

class SignalClient;
class RtcEngine : public SignalClientObserver,
                  public RtcSession::RtcSessionListener,
                  public webrtc::DataChannelObserver {
public:
	class RtcEngineListener {
	public:
		virtual ~RtcEngineListener() = default;

		virtual void ConnectedEvent(livekit::JoinResponse join_resp) = 0;
		virtual void
		ParticipantUpdateEvent(const std::vector<livekit::ParticipantInfo>& updates) = 0;
		virtual void
		MediaTrackEvent(webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track) = 0;
		virtual void DataPacketEvent(const livekit::DataPacket& packet) = 0;
		virtual void RemoteMuteChangedEvent(const std::string& sid, bool muted) = 0;
		virtual void LocalTrackUnpublishedEvent(const std::string& sid) = 0;
		virtual void SpeakersChangedEvent(const std::vector<livekit::SpeakerInfo>& updates) = 0;
		virtual void RoomUpdateEvent(const livekit::Room& update) = 0;
		virtual void
		ConnectionQualityEvent(const std::vector<livekit::ConnectionQualityInfo>& updates) = 0;
		virtual void SignalDisconnectedEvent() = 0;
		virtual void ReconnectingEvent() = 0;
		virtual void ReconnectedEvent(livekit::JoinResponse join_resp) = 0;
	};

	RtcEngine();
	~RtcEngine();

	livekit::JoinResponse Connect(std::string url, std::string token, EngineOptions options);
	void Disconnect();

	void SetRoomObserver(RtcEngineListener* listener);

	std::shared_ptr<PeerTransportFactory> GetSessionPeerTransportFactory();

	std::optional<livekit::TrackInfo> AddTrack(const livekit::AddTrackRequest& req);

	webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>
	CreateSender(LocalTrack* track, TrackPublishOptions options,
	             std::vector<webrtc::RtpEncodingParameters> send_encodings);
	bool RemoveSender(LocalTrack* track);

	void PublisherNegotiationNeeded();
	bool SendDataPacket(const livekit::DataPacket& packet, bool reliable);
	bool SetTrackMuted(const std::string& track_sid, bool muted);
	bool SetTrackSubscribed(const std::string& participant_sid, const std::string& track_sid,
	                        bool subscribed);
	bool UpdateLocalMetadata(const std::string& metadata, const std::string& name,
	                         const std::map<std::string, std::string>& attributes);
	// Retained internally so connection recovery can authenticate with the newest server-issued
	// token instead of the token used for the initial join.
	std::string AccessTokenForReconnect() const;
	bool SimulateSignalDisconnectForTesting();

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
	OnStateChange(RtcSession::State connection_state,
	              webrtc::PeerConnectionInterface::PeerConnectionState pub_state,
	              webrtc::PeerConnectionInterface::PeerConnectionState sub_state) override;

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

	/* Pure virtual methods inherited from webrtc::DataChannelObserver. */
	void OnStateChange() override;
	void OnMessage(const webrtc::DataBuffer& buffer) override;
	void OnBufferedAmountChange(uint64_t sent_data_size) override;

private:
	livekit::JoinResponse ConnectTransport(const std::string& url, const std::string& token,
	                                       const EngineOptions& options);
	void ResetTransport(bool send_leave);
	std::shared_ptr<SignalClient> SignalClientSnapshot() const;
	void StartRecovery();
	void RunRecovery();
	void StopRecovery();
	void negotiate();
	void createDataChannels();
	void registerDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface> channel,
	                         bool publisher_channel = false);
	void unregisterDataChannels();

private:
	std::atomic<RtcEngineListener*> room_listener_{nullptr};
	mutable std::mutex session_lock_;
	mutable std::mutex signal_client_lock_;
	std::shared_ptr<SignalClient> signal_client_;
	std::unique_ptr<RtcSession> rtc_session_;
	// Local media tracks are created by this factory. Keep it stable while replacing peer
	// connections so those tracks can be republished safely after a full reconnect.
	std::shared_ptr<PeerTransportFactory> peer_factory_;
	bool is_subscriber_primary_;
	webrtc::scoped_refptr<webrtc::DataChannelInterface> lossyDC_ = nullptr;
	webrtc::scoped_refptr<webrtc::DataChannelInterface> reliableDC_ = nullptr;
	std::mutex data_channels_lock_;
	std::vector<webrtc::scoped_refptr<webrtc::DataChannelInterface>> data_channels_;

	livekit::JoinResponse join_resp_;
	mutable std::mutex pending_track_resolvers_lock_;
	std::map<std::string, std::promise<livekit::TrackInfo>> pending_track_resolvers_;
	std::mutex initial_negotiation_mutex_;
	std::thread initial_negotiation_thread_;
	mutable std::mutex access_token_mutex_;
	std::string access_token_;
	mutable std::mutex connection_params_mutex_;
	std::string connection_url_;
	EngineOptions connection_options_;
	std::atomic<bool> recovery_allowed_{false};
	std::atomic<bool> recovery_stop_{false};
	std::atomic<bool> recovery_in_progress_{false};
	std::atomic<bool> recovering_connection_{false};
	std::mutex recovery_thread_mutex_;
	std::thread recovery_thread_;
	std::mutex rtc_connected_mutex_;
	std::condition_variable rtc_connected_cv_;
	bool rtc_connected_ = false;
};

} // namespace core
} // namespace livekit

#endif //
