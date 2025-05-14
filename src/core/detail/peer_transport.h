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

#ifndef _LKC_CORE_DETAIL_PEER_TRANSPORT_H_
#define _LKC_CORE_DETAIL_PEER_TRANSPORT_H_

#include "livekit_rtc.pb.h"

#include "api/create_peerconnection_factory.h"
#include "api/peer_connection_interface.h"

#include <future> // std::promise, std::future
#include <memory> // std::unique_ptr

namespace livekit {
namespace core {

std::unique_ptr<webrtc::SessionDescriptionInterface> ConvertSdp(webrtc::SdpType type,
                                                                const std::string& sdp);

class PeerTransport : public webrtc::PeerConnectionObserver {
public:
	enum class Target { UNKNOWN = 0, PUBLISHER, SUBSCRIBER };
	enum class SdpType : uint8_t { OFFER = 0, PRANSWER, ANSWER };

	static std::map<Target, const std::string> target2String;
	static std::map<webrtc::PeerConnectionInterface::PeerConnectionState, const std::string>
	    peerConnectionState2String;
	static std::map<webrtc::PeerConnectionInterface::IceConnectionState, const std::string>
	    iceConnectionState2String;
	static std::map<webrtc::PeerConnectionInterface::IceGatheringState, const std::string>
	    iceGatheringState2String;
	static std::map<webrtc::PeerConnectionInterface::SignalingState, const std::string>
	    signalingState2String;

	class SetLocalDescriptionObserver : public webrtc::SetLocalDescriptionObserverInterface {
	public:
		SetLocalDescriptionObserver() = default;
		~SetLocalDescriptionObserver() override = default;

		std::future<void> GetFuture();
		void Reject(const std::string& error);

		/* Virtual methods inherited from webrtc::SetLocalDescriptionObserver. */
	public:
		void OnSetLocalDescriptionComplete(webrtc::RTCError error) override;

	private:
		std::promise<void> promise;
	};

	class SetRemoteDescriptionObserver : public webrtc::SetRemoteDescriptionObserverInterface {
	public:
		SetRemoteDescriptionObserver() = default;
		~SetRemoteDescriptionObserver() override = default;

		std::future<void> GetFuture();
		void Reject(const std::string& error);

		/* Virtual methods inherited from webrtc::SetRemoteDescriptionObserver. */
	public:
		void OnSetRemoteDescriptionComplete(webrtc::RTCError error) override;

	private:
		std::promise<void> promise;
	};

	class SetSessionDescriptionObserver : public webrtc::SetSessionDescriptionObserver {
	public:
		SetSessionDescriptionObserver() = default;
		~SetSessionDescriptionObserver() override = default;

		std::future<void> GetFuture();
		void Reject(const std::string& error);

		/* Virtual methods inherited from webrtc::SetSessionDescriptionObserver. */
	public:
		void OnSuccess() override;
		void OnFailure(webrtc::RTCError error) override;

	private:
		std::promise<void> promise;
	};

	class CreateSessionDescriptionObserver : public webrtc::CreateSessionDescriptionObserver {
	public:
		CreateSessionDescriptionObserver() = default;
		~CreateSessionDescriptionObserver() override = default;

		std::future<std::string> GetFuture();
		void Reject(const std::string& error);

		/* Virtual methods inherited from webrtc::CreateSessionDescriptionObserver. */
	public:
		void OnSuccess(webrtc::SessionDescriptionInterface* desc) override;
		void OnFailure(webrtc::RTCError error) override;

	private:
		std::promise<std::string> promise;
	};

	class PeerTransportListener {
	public:
		virtual ~PeerTransportListener() = default;

		virtual void OnOffer(Target target,
		                     std::unique_ptr<webrtc::SessionDescriptionInterface> offer) = 0;
		virtual void
		OnSignalingChange(Target target,
		                  webrtc::PeerConnectionInterface::SignalingState newState) = 0;
		virtual void
		OnConnectionChange(Target target,
		                   webrtc::PeerConnectionInterface::PeerConnectionState new_state) = 0;
		virtual void OnAddStream(Target target,
		                         rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) = 0;
		virtual void OnRemoveStream(Target target,
		                            rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) = 0;
		virtual void
		OnDataChannel(Target target,
		              rtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel) = 0;
		virtual void OnRenegotiationNeeded(Target target) = 0;
		virtual void
		OnIceConnectionChange(Target target,
		                      webrtc::PeerConnectionInterface::IceConnectionState newState) = 0;
		virtual void
		OnIceGatheringChange(Target target,
		                     webrtc::PeerConnectionInterface::IceGatheringState newState) = 0;
		virtual void OnIceCandidate(Target target,
		                            const webrtc::IceCandidateInterface* candidate) = 0;
		virtual void OnIceCandidatesRemoved(Target target,
		                                    const std::vector<cricket::Candidate>& candidates) = 0;
		virtual void OnIceConnectionReceivingChange(Target target, bool receiving) = 0;
		virtual void OnIceCandidateError(Target target, const std::string& address, int port,
		                                 const std::string& url, int error_code,
		                                 const std::string& error_text) = 0;
		virtual void OnAddTrack(
		    Target target, rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
		    const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) = 0;
		virtual void OnTrack(Target target,
		                     rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) = 0;
		virtual void OnRemoveTrack(Target target,
		                           rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) = 0;
		virtual void OnInterestingUsage(Target target, int usagePattern) = 0;
	};

public:
	PeerTransport(Target target, webrtc::PeerConnectionInterface::RTCConfiguration& rtc_config,
	              webrtc::PeerConnectionFactoryInterface* factory);
	~PeerTransport();

	bool Init();

	void AddPeerTransportListener(PeerTransport::PeerTransportListener* listener);
	void RemovePeerTransportListener();

public:
	std::string CreateOffer(const webrtc::PeerConnectionInterface::RTCOfferAnswerOptions& options);

	std::string CreateAnswer(const webrtc::PeerConnectionInterface::RTCOfferAnswerOptions& options);

	void SetLocalDescription(std::unique_ptr<webrtc::SessionDescriptionInterface> desc);
	void SetRemoteDescription(std::unique_ptr<webrtc::SessionDescriptionInterface> desc);

	const std::string GetLocalDescription();
	const std::string GetRemoteDescription();
	const std::string GetCurrentLocalDescription();
	const std::string GetCurrentRemoteDescription();
	const std::string GetPendingLocalDescription();
	const std::string GetPendingRemoteDescription();

	std::vector<rtc::scoped_refptr<webrtc::RtpTransceiverInterface>> GetTransceivers() const;
	rtc::scoped_refptr<webrtc::RtpTransceiverInterface>
	AddTransceiver(cricket::MediaType mediaType);
	rtc::scoped_refptr<webrtc::RtpTransceiverInterface>
	AddTransceiver(rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track,
	               webrtc::RtpTransceiverInit rtpTransceiverInit);

	rtc::scoped_refptr<webrtc::DataChannelInterface>
	CreateDataChannel(const std::string& label, const webrtc::DataChannelInit* config);

	void AddIceCandidate(const std::string& candidate_json_str);

	bool Negotiate();

	bool TestFlushIceCandidate();

private:
	bool create_peer_connection();

	void
	createAndSendPublisherOffer(webrtc::PeerConnectionInterface::RTCOfferAnswerOptions& options);

	// peer connection observer
private:
	void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState newState) override;
	void
	OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState new_state) override;
	void OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;
	void OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;
	void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel) override;
	void OnRenegotiationNeeded() override;
	void
	OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState newState) override;
	void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState newState) override;
	void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;
	void OnIceCandidatesRemoved(const std::vector<cricket::Candidate>& candidates) override;
	void OnIceConnectionReceivingChange(bool receiving) override;
	void OnIceCandidateError(const std::string& address, int port, const std::string& url,
	                         int error_code, const std::string& error_text) override;
	void OnAddTrack(
	    rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
	    const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) override;
	void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;
	void OnRemoveTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override;
	void OnInterestingUsage(int usagePattern) override;

private:
	Target target_;
	webrtc::PeerConnectionInterface::RTCConfiguration rtc_config_;
	// Signaling and worker threads.
	std::unique_ptr<rtc::Thread> network_thread_;
	std::unique_ptr<rtc::Thread> signaling_thread_;
	std::unique_ptr<rtc::Thread> worker_thread_;
	rtc::scoped_refptr<webrtc::AudioDeviceModule> audio_device_;
	webrtc::TaskQueueFactory* task_queue_factory_ = nullptr;
	// PeerConnection factory.
	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> pc_factory_;

	// PeerConnection instance.
	rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_ = nullptr;
	mutable std::mutex pc_lock_;
	mutable std::mutex pending_candidates_lock_;
	std::vector<std::string> pending_candidates_;
	std::atomic<bool> restarting_ice_ = false;
	PeerTransport::PeerTransportListener* listener_ = nullptr;
};
} // namespace core
} // namespace livekit

#endif //
