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
#include "peer_transport.h"

#include "api/rtc_event_log/rtc_event_log_factory.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_decoder_factory_template.h"
#include "api/video_codecs/video_decoder_factory_template_dav1d_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_open_h264_adapter.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory_template_libaom_av1_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_open_h264_adapter.h"
#include <api/audio_codecs/builtin_audio_decoder_factory.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/create_peerconnection_factory.h>
#include <api/video_codecs/builtin_video_decoder_factory.h>
#include <api/video_codecs/builtin_video_encoder_factory.h>
#include <rtc_base/ssl_adapter.h>

namespace livekit {
namespace core {

PeerTransport::PeerTransport(webrtc::PeerConnectionInterface::RTCConfiguration& rtc_config,
                             webrtc::PeerConnectionFactoryInterface* factory)
    : rtc_config_(rtc_config) {
	if (factory == nullptr) {
		this->network_thread_ = rtc::Thread::CreateWithSocketServer();
		this->signaling_thread_ = rtc::Thread::Create();
		this->worker_thread_ = rtc::Thread::Create();

		this->network_thread_->SetName("network_thread", &network_thread_);
		this->signaling_thread_->SetName("signaling_thread", &signaling_thread_);
		this->worker_thread_->SetName("worker_thread", &worker_thread_);

		if (!this->network_thread_->Start() || !this->signaling_thread_->Start() ||
		    !this->worker_thread_->Start()) {
			throw("thread start errored");
		}

		this->pc_factory_ = webrtc::CreatePeerConnectionFactory(
		    this->network_thread_.get(), this->worker_thread_.get(), this->signaling_thread_.get(),
		    nullptr /*default_adm*/, webrtc::CreateBuiltinAudioEncoderFactory(),
		    webrtc::CreateBuiltinAudioDecoderFactory(),
		    std::make_unique<webrtc::VideoEncoderFactoryTemplate<
		        webrtc::LibvpxVp8EncoderTemplateAdapter, webrtc::LibvpxVp9EncoderTemplateAdapter,
		        webrtc::OpenH264EncoderTemplateAdapter, webrtc::LibaomAv1EncoderTemplateAdapter>>(),
		    std::make_unique<webrtc::VideoDecoderFactoryTemplate<
		        webrtc::LibvpxVp8DecoderTemplateAdapter, webrtc::LibvpxVp9DecoderTemplateAdapter,
		        webrtc::OpenH264DecoderTemplateAdapter, webrtc::Dav1dDecoderTemplateAdapter>>(),
		    nullptr /*audio_mixer*/, nullptr /*audio_processing*/);
	} else {
		this->pc_factory_ = rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>(factory);
	}

	if (this->pc_factory_.get() == nullptr) {
		throw("peerconnect factory errored");
	}
}

PeerTransport::~PeerTransport() { std::cout << "PeerTransport::~PeerTransport()" << std::endl; }

bool PeerTransport::Init(PrivateListener* privateListener) {
	pc_ = create_peer_connection(privateListener);
	return true;
}

rtc::scoped_refptr<webrtc::PeerConnectionInterface>
PeerTransport::create_peer_connection(PrivateListener* privateListener) {
	return this->pc_factory_->CreatePeerConnection(rtc_config_, nullptr, nullptr, privateListener);
}

/* PeerConnection::PrivateListener */

/**
 * Triggered when the SignalingState changed.
 */
void PeerTransport::PrivateListener::OnSignalingChange(
    webrtc::PeerConnectionInterface::SignalingState newState) {}

/**
 * Triggered when the ConnectionState changed.
 */
void PeerTransport::PrivateListener ::OnConnectionChange(
    webrtc::PeerConnectionInterface::PeerConnectionState new_state) {}

/**
 * Triggered when media is received on a new stream from remote peer.
 */
void PeerTransport::PrivateListener::OnAddStream(
    rtc::scoped_refptr<webrtc::MediaStreamInterface> /*stream*/) {}

/**
 * Triggered when a remote peer closes a stream.
 */
void PeerTransport::PrivateListener::OnRemoveStream(
    rtc::scoped_refptr<webrtc::MediaStreamInterface> /*stream*/) {}

/**
 * Triggered when a remote peer opens a data channel.
 */
void PeerTransport::PrivateListener::OnDataChannel(
    rtc::scoped_refptr<webrtc::DataChannelInterface> /*dataChannel*/) {}

/**
 * Triggered when renegotiation is needed. For example, an ICE restart has begun.
 */
void PeerTransport::PrivateListener::OnRenegotiationNeeded() {}

/**
 * Triggered any time the IceConnectionState changes.
 *
 * Note that our ICE states lag behind the standard slightly. The most
 * notable differences include the fact that "failed" occurs after 15
 * seconds, not 30, and this actually represents a combination ICE + DTLS
 * state, so it may be "failed" if DTLS fails while ICE succeeds.
 */
void PeerTransport::PrivateListener::OnIceConnectionChange(
    webrtc::PeerConnectionInterface::IceConnectionState newState) {}

/**
 * Triggered any time the IceGatheringState changes.
 */
void PeerTransport::PrivateListener::OnIceGatheringChange(
    webrtc::PeerConnectionInterface::IceGatheringState newState) {}

/**
 * Triggered when a new ICE candidate has been gathered.
 */
void PeerTransport::PrivateListener::OnIceCandidate(
    const webrtc::IceCandidateInterface* candidate) {

	std::string candidateStr;

	candidate->ToString(&candidateStr);
}

/**
 * Triggered when the ICE candidates have been removed.
 */
void PeerTransport::PrivateListener::OnIceCandidatesRemoved(
    const std::vector<cricket::Candidate>& /*candidates*/) {}

/**
 * Triggered when the ICE connection receiving status changes.
 */
void PeerTransport::PrivateListener::OnIceConnectionReceivingChange(bool /*receiving*/) {}

/**
 * Triggered when the ICE connection receiving error.
 */
void PeerTransport::PrivateListener::OnIceCandidateError(const std::string& address, int port,
                                                         const std::string& url, int error_code,
                                                         const std::string& error_text) {}
/**
 * Triggered when a receiver and its track are created.
 *
 * Note: This is called with both Plan B and Unified Plan semantics. Unified
 * Plan users should prefer OnTrack, OnAddTrack is only called as backwards
 * compatibility (and is called in the exact same situations as OnTrack).
 */
void PeerTransport::PrivateListener::OnAddTrack(
    rtc::scoped_refptr<webrtc::RtpReceiverInterface> /*receiver*/,
    const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>& /*streams*/) {}

/**
 * Triggered when signaling indicates a transceiver will be receiving
 *
 * media from the remote endpoint. This is fired during a call to
 * SetRemoteDescription. The receiving track can be accessed by:
 * transceiver->receiver()->track() and its associated streams by
 * transceiver->receiver()->streams().
 *
 * NOTE: This will only be called if Unified Plan semantics are specified.
 * This behavior is specified in section 2.2.8.2.5 of the "Set the
 * RTCSessionDescription" algorithm:
 *   https://w3c.github.io/webrtc-pc/#set-description
 */
void PeerTransport::PrivateListener::OnTrack(
    rtc::scoped_refptr<webrtc::RtpTransceiverInterface> /*transceiver*/) {}

/**
 * Triggered when signaling indicates that media will no longer be received on a
 * track.
 *
 * With Plan B semantics, the given receiver will have been removed from the
 * PeerConnection and the track muted.
 * With Unified Plan semantics, the receiver will remain but the transceiver
 * will have changed direction to either sendonly or inactive.
 *   https://w3c.github.io/webrtc-pc/#process-remote-track-removal
 */
void PeerTransport::PrivateListener::OnRemoveTrack(
    rtc::scoped_refptr<webrtc::RtpReceiverInterface> /*receiver*/) {}

/**
 * Triggered when an interesting usage is detected by WebRTC.
 *
 * An appropriate action is to add information about the context of the
 * PeerConnection and write the event to some kind of "interesting events"
 * log function.
 * The heuristics for defining what constitutes "interesting" are
 * implementation-defined.
 */
void PeerTransport::PrivateListener::OnInterestingUsage(int /*usagePattern*/) {}

} // namespace core
} // namespace livekit
