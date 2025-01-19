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

#include "api/peer_connection_interface.h"
#include "api/create_peerconnection_factory.h"

namespace livekit {
namespace core {
class PeerTransport {
public:
	class PrivateListener : public webrtc::PeerConnectionObserver {
		/* Virtual methods inherited from PeerConnectionObserver. */
	public:
		void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState newState) override;
		void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState new_state) override;
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

public:
	PeerTransport(webrtc::PeerConnectionInterface::RTCConfiguration rtc_config, webrtc::PeerConnectionFactoryInterface* factory);
	~PeerTransport();

    bool Init(PrivateListener* privateListener);

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
};
}
} // namespace livekit

#endif //
