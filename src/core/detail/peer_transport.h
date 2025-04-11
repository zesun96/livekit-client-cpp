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
class PeerTransport {
public:
	enum class SdpType : uint8_t { OFFER = 0, PRANSWER, ANSWER };
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

	class PrivateListener : public webrtc::PeerConnectionObserver {
		/* Virtual methods inherited from PeerConnectionObserver. */
	public:
		void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState newState) override;
		void
		OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState new_state) override;
		void OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;
		void OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;
		void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel) override;
		void OnRenegotiationNeeded() override;
		void OnIceConnectionChange(
		    webrtc::PeerConnectionInterface::IceConnectionState newState) override;
		void
		OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState newState) override;
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
	};

	class PeerTransportListener {
	public:
		virtual ~PeerTransportListener() = default;
		virtual void OnOffer(std::unique_ptr<webrtc::SessionDescriptionInterface> offer) = 0;
	};

public:
	PeerTransport(webrtc::PeerConnectionInterface::RTCConfiguration& rtc_config,
	              webrtc::PeerConnectionFactoryInterface* factory);
	~PeerTransport();

	bool Init(PrivateListener* privateListener);

	void AddPeerTransportListener(PeerTransport::PeerTransportListener* listener);
	void RemovePeerTransportListener();

public:
	void SetRemoteDescription(std::unique_ptr<webrtc::SessionDescriptionInterface> offer);

	std::unique_ptr<webrtc::SessionDescriptionInterface>
	CreateAnswer(const webrtc::PeerConnectionInterface::RTCOfferAnswerOptions& options);

	void SetLocalDescription(std::unique_ptr<webrtc::SessionDescriptionInterface> desc);

	std::string CreateOffer(const webrtc::PeerConnectionInterface::RTCOfferAnswerOptions& options);

	void AddIceCandidate(const std::string& candidate_json_str);

	bool Negotiate();

private:
	rtc::scoped_refptr<webrtc::PeerConnectionInterface>
	create_peer_connection(PrivateListener* privateListener);

private:
	webrtc::PeerConnectionInterface::RTCConfiguration rtc_config_;
	// Signaling and worker threads.
	std::unique_ptr<rtc::Thread> network_thread_;
	std::unique_ptr<rtc::Thread> signaling_thread_;
	std::unique_ptr<rtc::Thread> worker_thread_;
	rtc::scoped_refptr<webrtc::AudioDeviceModule> audio_device_;
	webrtc::TaskQueueFactory* task_queue_factory_;
	// PeerConnection factory.
	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> pc_factory_;

	// PeerConnection instance.
	rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;

	PeerTransport::PeerTransportListener* listener_ = nullptr;
};
} // namespace core
} // namespace livekit

#endif //
