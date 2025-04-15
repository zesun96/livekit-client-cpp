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

#include <nlohmann/json.hpp>

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
#include <api/jsep.h>
#include <api/video_codecs/builtin_video_decoder_factory.h>
#include <api/video_codecs/builtin_video_encoder_factory.h>
#include <rtc_base/ssl_adapter.h>

namespace {
std::string serialize_sdp_error(webrtc::SdpParseError error) {
	std::stringstream ss;
	ss << std::hex << std::setfill('0');
	ss << std::setw(8) << (uint32_t)error.line.length();
	ss << std::dec << std::setw(1) << error.line;
	ss << std::dec << std::setw(1) << error.description;
	return ss.str();
}

} // namespace

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

		// Set SDP semantics to Unified Plan.
		rtc_config_.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;

		// this->pc_factory_ = webrtc::CreatePeerConnectionFactory(
		//     this->network_thread_.get(), this->worker_thread_.get(),
		//     this->signaling_thread_.get(), nullptr /*default_adm*/,
		//     webrtc::CreateBuiltinAudioEncoderFactory(),
		//     webrtc::CreateBuiltinAudioDecoderFactory(),
		//     std::make_unique<webrtc::VideoEncoderFactoryTemplate<
		//         webrtc::LibvpxVp8EncoderTemplateAdapter, webrtc::LibvpxVp9EncoderTemplateAdapter,
		//         webrtc::OpenH264EncoderTemplateAdapter,
		//         webrtc::LibaomAv1EncoderTemplateAdapter>>(),
		//     std::make_unique<webrtc::VideoDecoderFactoryTemplate<
		//         webrtc::LibvpxVp8DecoderTemplateAdapter, webrtc::LibvpxVp9DecoderTemplateAdapter,
		//         webrtc::OpenH264DecoderTemplateAdapter, webrtc::Dav1dDecoderTemplateAdapter>>(),
		//     nullptr /*audio_mixer*/, nullptr /*audio_processing*/);

		this->pc_factory_ = webrtc::CreatePeerConnectionFactory(
		    this->network_thread_.get(), this->worker_thread_.get(), this->signaling_thread_.get(),
		    nullptr /*default_adm*/, webrtc::CreateBuiltinAudioEncoderFactory(),
		    webrtc::CreateBuiltinAudioDecoderFactory(), nullptr, nullptr, nullptr /*audio_mixer*/,
		    nullptr /*audio_processing*/);
	} else {
		this->pc_factory_ = rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>(factory);
	}

	if (this->pc_factory_.get() == nullptr) {
		throw("peerconnect factory errored");
	}
}

PeerTransport::~PeerTransport() {
	std::cout << "PeerTransport::~PeerTransport()" << std::endl;
	this->pc_->Close();
}

bool PeerTransport::Init(PrivateListener* privateListener) {
	pc_ = create_peer_connection(privateListener);
	return true;
}

void PeerTransport::AddPeerTransportListener(PeerTransport::PeerTransportListener* listener) {
	this->listener_ = listener;
}

void PeerTransport::RemovePeerTransportListener() { this->listener_ = nullptr; }

std::string
PeerTransport::CreateOffer(const webrtc::PeerConnectionInterface::RTCOfferAnswerOptions& options) {

	CreateSessionDescriptionObserver* sessionDescriptionObserver =
	    new rtc::RefCountedObject<CreateSessionDescriptionObserver>();
	auto future = sessionDescriptionObserver->GetFuture();

	this->pc_->CreateOffer(sessionDescriptionObserver, options);

	return future.get();
}

std::unique_ptr<webrtc::SessionDescriptionInterface>
PeerTransport::CreateAnswer(const webrtc::PeerConnectionInterface::RTCOfferAnswerOptions& options) {
	CreateSessionDescriptionObserver* sessionDescriptionObserver =
	    new rtc::RefCountedObject<CreateSessionDescriptionObserver>();
	auto future = sessionDescriptionObserver->GetFuture();
	this->pc_->CreateAnswer(sessionDescriptionObserver, options);
	std::string answer = future.get();

	webrtc::SdpParseError error;
	std::unique_ptr<webrtc::SessionDescriptionInterface> sessionDescription =
	    webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer, answer, &error);
	if (sessionDescription == nullptr) {
		throw std::runtime_error(serialize_sdp_error(error));
	}
	return sessionDescription;
}

void PeerTransport::SetLocalDescription(std::unique_ptr<webrtc::SessionDescriptionInterface> desc) {
	rtc::scoped_refptr<SetLocalDescriptionObserver> observer(
	    new rtc::RefCountedObject<SetLocalDescriptionObserver>());
	auto future = observer->GetFuture();
	this->pc_->SetLocalDescription(std::move(desc), observer);
	return future.get();
}

void PeerTransport::SetRemoteDescription(
    std::unique_ptr<webrtc::SessionDescriptionInterface> offer) {
	rtc::scoped_refptr<SetRemoteDescriptionObserver> observer(
	    new rtc::RefCountedObject<SetRemoteDescriptionObserver>());
	auto future = observer->GetFuture();

	this->pc_->SetRemoteDescription(std::move(offer), observer);
	return future.get();
}

const std::string PeerTransport::GetLocalDescription() {
	auto desc = this->pc_->local_description();
	std::string sdp;

	desc->ToString(&sdp);

	return sdp;
}

const std::string PeerTransport::GetRemoteDescription() {
	auto desc = this->pc_->remote_description();
	std::string sdp;

	desc->ToString(&sdp);

	return sdp;
}

std::vector<rtc::scoped_refptr<webrtc::RtpTransceiverInterface>>
PeerTransport::GetTransceivers() const {
	return this->pc_->GetTransceivers();
}

rtc::scoped_refptr<webrtc::RtpTransceiverInterface>
PeerTransport::AddTransceiver(cricket::MediaType mediaType) {
	auto result = this->pc_->AddTransceiver(mediaType);

	if (!result.ok()) {
		rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver = nullptr;

		return transceiver;
	}

	return result.value();
}

rtc::scoped_refptr<webrtc::RtpTransceiverInterface>
PeerTransport::AddTransceiver(rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track,
                              webrtc::RtpTransceiverInit rtpTransceiverInit) {
	/*
	 * Define a stream id so the generated local description is correct.
	 * - with a stream id:    "a=ssrc:<ssrc-id> mslabel:<value>"
	 * - without a stream id: "a=ssrc:<ssrc-id> mslabel:"
	 *
	 * The second is incorrect (https://tools.ietf.org/html/rfc5576#section-4.1)
	 */
	rtpTransceiverInit.stream_ids.emplace_back("0");

	auto result = this->pc_->AddTransceiver(
	    track, rtpTransceiverInit); // NOLINT(performance-unnecessary-value-param)

	if (!result.ok()) {
		rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver = nullptr;

		return transceiver;
	}

	return result.value();
}

rtc::scoped_refptr<webrtc::DataChannelInterface>
PeerTransport::CreateDataChannel(const std::string& label, const webrtc::DataChannelInit* config) {
	const auto result = this->pc_->CreateDataChannelOrError(label, config);

	if (result.ok()) {
		std::cout << "data channel created successfully" << std::endl;
	} else {
		throw std::runtime_error("data channel create errored");
	}

	return result.value();
}

void PeerTransport::AddIceCandidate(const std::string& candidate_json_str) {
	auto candidate_json = nlohmann::json::parse(candidate_json_str);
	std::string sdp_mid = candidate_json["sdpMid"];
	int sdp_mline_index = candidate_json["sdpMLineIndex"];
	std::string candidate_str = candidate_json["candidate"];
	webrtc::SdpParseError error;
	std::unique_ptr<webrtc::IceCandidateInterface> candidate(
	    webrtc::CreateIceCandidate(sdp_mid, sdp_mline_index, candidate_str, &error));
	if (!candidate.get()) {
		return;
	}
	this->pc_->AddIceCandidate(candidate.get());
	return;
}

bool PeerTransport::Negotiate() {
	// May throw.
	webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
	// options.offer_to_receive_audio = true;
	options.ice_restart = true;
	auto offer = this->CreateOffer(options);

	webrtc::SdpParseError error;
	std::unique_ptr<webrtc::SessionDescriptionInterface> sessionDescription =
	    webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, offer, &error);
	if (sessionDescription == nullptr) {
		throw std::runtime_error(serialize_sdp_error(error));
	}
	this->SetLocalDescription(std::move(sessionDescription));

	auto new_offer = this->GetLocalDescription();

	std::unique_ptr<webrtc::SessionDescriptionInterface> new_desc =
	    webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, new_offer, &error);
	if (new_desc == nullptr) {
		throw std::runtime_error(serialize_sdp_error(error));
	}

	if (this->listener_) {
		this->listener_->OnOffer(std::move(new_desc));
	}
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

/* SetLocalDescriptionObserver */
std::future<void> PeerTransport::SetLocalDescriptionObserver::GetFuture() {
	return this->promise.get_future();
}

void PeerTransport::SetLocalDescriptionObserver::Reject(const std::string& error) {

	this->promise.set_exception(std::make_exception_ptr(error.c_str()));
}

void PeerTransport::SetLocalDescriptionObserver::OnSetLocalDescriptionComplete(
    webrtc::RTCError error) {

	if (!error.ok()) {

		auto message = std::string(error.message());

		this->Reject(message);
	} else {
		this->promise.set_value();
	}
};

/* SetRemoteDescriptionObserver */

std::future<void> PeerTransport::SetRemoteDescriptionObserver::GetFuture() {

	return this->promise.get_future();
}

void PeerTransport::SetRemoteDescriptionObserver::Reject(const std::string& error) {

	this->promise.set_exception(std::make_exception_ptr(error.c_str()));
}

void PeerTransport::SetRemoteDescriptionObserver::OnSetRemoteDescriptionComplete(
    webrtc::RTCError error) {

	if (!error.ok()) {

		auto message = std::string(error.message());

		this->Reject(message);
	} else {
		this->promise.set_value();
	}
};

/* SetSessionDescriptionObserver */

std::future<void> PeerTransport::SetSessionDescriptionObserver::GetFuture() {
	return this->promise.get_future();
}

void PeerTransport::SetSessionDescriptionObserver::Reject(const std::string& error) {
	this->promise.set_exception(std::make_exception_ptr(error.c_str()));
}

void PeerTransport::SetSessionDescriptionObserver::OnSuccess() { this->promise.set_value(); };

void PeerTransport::SetSessionDescriptionObserver::OnFailure(webrtc::RTCError error) {
	auto message = std::string(error.message());

	this->Reject(message);
};

/* CreateSessionDescriptionObserver */

std::future<std::string> PeerTransport::CreateSessionDescriptionObserver::GetFuture() {

	return this->promise.get_future();
}

void PeerTransport::CreateSessionDescriptionObserver::Reject(const std::string& error) {

	this->promise.set_exception(std::make_exception_ptr(error.c_str()));
}

void PeerTransport::CreateSessionDescriptionObserver::OnSuccess(
    webrtc::SessionDescriptionInterface* desc) {

	// This callback should take the ownership of |desc|.
	std::unique_ptr<webrtc::SessionDescriptionInterface> ownedDesc(desc);

	std::string sdp;

	ownedDesc->ToString(&sdp);
	this->promise.set_value(sdp);
};

void PeerTransport::CreateSessionDescriptionObserver::OnFailure(webrtc::RTCError error) {
	auto message = std::string(error.message());

	this->Reject(message);
}

} // namespace core
} // namespace livekit
