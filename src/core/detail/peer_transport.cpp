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

std::unique_ptr<webrtc::IceCandidateInterface>
deserialize_ice_candidate(const std::string& candidate_str) {
	auto candidate_json = nlohmann::json::parse(candidate_str);
	std::string sdp_mid = candidate_json["sdpMid"];
	int sdp_mline_index = candidate_json["sdpMLineIndex"];
	std::string candidate = candidate_json["candidate"];

	webrtc::SdpParseError error;
	std::unique_ptr<webrtc::IceCandidateInterface> candidate_ptr(
	    webrtc::CreateIceCandidate(sdp_mid, sdp_mline_index, candidate, &error));
	if (!candidate_ptr.get()) {
		throw("deserialize ice candidate failed");
	}
	return std::move(candidate_ptr);
}

} // namespace

namespace livekit {
namespace core {

std::map<PeerTransport::Target, const std::string> PeerTransport::target2String = {
    {PeerTransport::Target::UNKNOWN, "unknown"},
    {PeerTransport::Target::PUBLISHER, "publisher"},
    {PeerTransport::Target::SUBSCRIBER, "subscriber"},
};

std::map<webrtc::PeerConnectionInterface::PeerConnectionState, const std::string>
    PeerTransport::peerConnectionState2String = {
        {webrtc::PeerConnectionInterface::PeerConnectionState::kNew, "new"},
        {webrtc::PeerConnectionInterface::PeerConnectionState::kConnecting, "connecting"},
        {webrtc::PeerConnectionInterface::PeerConnectionState::kConnected, "connected"},
        {webrtc::PeerConnectionInterface::PeerConnectionState::kDisconnected, "disconnected"},
        {webrtc::PeerConnectionInterface::PeerConnectionState::kFailed, "failed"},
        {webrtc::PeerConnectionInterface::PeerConnectionState::kClosed, "closed"},
};

std::map<webrtc::PeerConnectionInterface::IceConnectionState, const std::string>
    PeerTransport::iceConnectionState2String = {
        {webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionNew, "new"},
        {webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionChecking, "checking"},
        {webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionConnected, "connected"},
        {webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionCompleted, "completed"},
        {webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionFailed, "failed"},
        {webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionDisconnected,
         "disconnected"},
        {webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionClosed, "closed"},
};

std::map<webrtc::PeerConnectionInterface::IceGatheringState, const std::string>
    PeerTransport::iceGatheringState2String = {
        {webrtc::PeerConnectionInterface::IceGatheringState::kIceGatheringNew, "new"},
        {webrtc::PeerConnectionInterface::IceGatheringState::kIceGatheringGathering, "gathering"},
        {webrtc::PeerConnectionInterface::IceGatheringState::kIceGatheringComplete, "complete"},
};

std::map<webrtc::PeerConnectionInterface::SignalingState, const std::string>
    PeerTransport::signalingState2String = {
        {webrtc::PeerConnectionInterface::SignalingState::kStable, "stable"},
        {webrtc::PeerConnectionInterface::SignalingState::kHaveLocalOffer, "have-local-offer"},
        {webrtc::PeerConnectionInterface::SignalingState::kHaveLocalPrAnswer,
         "have-local-pranswer"},
        {webrtc::PeerConnectionInterface::SignalingState::kHaveRemoteOffer, "have-remote-offer"},
        {webrtc::PeerConnectionInterface::SignalingState::kHaveRemotePrAnswer,
         "have-remote-pranswer"},
        {webrtc::PeerConnectionInterface::SignalingState::kClosed, "closed"},
};

std::unique_ptr<webrtc::SessionDescriptionInterface> ConvertSdp(webrtc::SdpType type,
                                                                const std::string& sdp) {
	webrtc::SdpParseError error;
	std::unique_ptr<webrtc::SessionDescriptionInterface> sessionDescription =
	    webrtc::CreateSessionDescription(type, sdp, &error);
	if (sessionDescription == nullptr) {
		throw std::runtime_error(serialize_sdp_error(error));
	}
	return std::move(sessionDescription);
}

PeerTransport::PeerTransport(Target target,
                             webrtc::PeerConnectionInterface::RTCConfiguration& rtc_config,
                             webrtc::PeerConnectionFactoryInterface* factory)
    : target_(target), rtc_config_(rtc_config) {
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
	if (this->pc_ != nullptr) {
		this->pc_->Close();
	}
	if (this->worker_thread_ != nullptr) {
		this->worker_thread_->Stop();
	}
	if (this->signaling_thread_ != nullptr) {
		this->signaling_thread_->Stop();
		;
	}
	if (this->network_thread_ != nullptr) {
		this->network_thread_->Stop();
	}
}

bool PeerTransport::Init() { return create_peer_connection(); }

void PeerTransport::AddPeerTransportListener(PeerTransport::PeerTransportListener* listener) {
	this->listener_ = listener;
}

void PeerTransport::RemovePeerTransportListener() { this->listener_ = nullptr; }

std::string
PeerTransport::CreateOffer(const webrtc::PeerConnectionInterface::RTCOfferAnswerOptions& options) {
	std::lock_guard<std::mutex> guard(pc_lock_);
	CreateSessionDescriptionObserver* sessionDescriptionObserver =
	    new rtc::RefCountedObject<CreateSessionDescriptionObserver>();
	auto future = sessionDescriptionObserver->GetFuture();

	this->pc_->CreateOffer(sessionDescriptionObserver, options);

	return future.get();
}

std::string
PeerTransport::CreateAnswer(const webrtc::PeerConnectionInterface::RTCOfferAnswerOptions& options) {
	std::lock_guard<std::mutex> guard(pc_lock_);
	CreateSessionDescriptionObserver* sessionDescriptionObserver =
	    new rtc::RefCountedObject<CreateSessionDescriptionObserver>();
	auto future = sessionDescriptionObserver->GetFuture();
	this->pc_->CreateAnswer(sessionDescriptionObserver, options);
	std::string answer = future.get();

	return answer;
}

void PeerTransport::SetLocalDescription(std::unique_ptr<webrtc::SessionDescriptionInterface> desc) {
	std::lock_guard<std::mutex> guard(pc_lock_);
	rtc::scoped_refptr<SetLocalDescriptionObserver> observer(
	    new rtc::RefCountedObject<SetLocalDescriptionObserver>());
	auto future = observer->GetFuture();
	this->pc_->SetLocalDescription(std::move(desc), observer);
	return future.get();
}

void PeerTransport::SetRemoteDescription(
    std::unique_ptr<webrtc::SessionDescriptionInterface> desc) {
	std::lock_guard<std::mutex> guard(pc_lock_);
	try {
		rtc::scoped_refptr<SetRemoteDescriptionObserver> observer(
		    new rtc::RefCountedObject<SetRemoteDescriptionObserver>());
		auto future = observer->GetFuture();

		this->pc_->SetRemoteDescription(std::move(desc), observer);
		future.get();
	} catch (const std::exception& err) {
		std::cerr << err.what() << '\n';
		return;
	}

	{
		std::lock_guard<std::mutex> guard(pending_candidates_lock_);
		for (auto& candidate_str : pending_candidates_) {
			try {
				// signaling_thread_->PostTask([this, candidate_str]() {
				// 	auto candidate = deserialize_ice_candidate(candidate_str);
				// 	this->pc_->AddIceCandidate(std::move(candidate), [](webrtc::RTCError error) {
				// 		if (error.ok()) {
				// 			std::cout << "ICE Candidate added successfully." << std::endl;
				// 		} else {
				// 			switch (error.type()) {
				// 			case webrtc::RTCErrorType::INVALID_PARAMETER:
				// 				std::cerr << "Invalid candidate format: " << error.message()
				// 				          << std::endl;
				// 				break;
				// 			case webrtc::RTCErrorType::INVALID_STATE:
				// 				std::cerr << "Call AddIceCandidate after SetRemoteDescription!"
				// 				          << std::endl;
				// 				break;
				// 			default:
				// 				std::cerr << "Unexpected error: " << error.message() << std::endl;
				// 			}
				// 		}
				// 	});
				// });

				auto candidate = deserialize_ice_candidate(candidate_str);
				this->pc_->AddIceCandidate(std::move(candidate), [](webrtc::RTCError error) {
					if (error.ok()) {
						std::cout << "ICE Candidate added successfully." << std::endl;
					} else {
						switch (error.type()) {
						case webrtc::RTCErrorType::INVALID_PARAMETER:
							std::cerr << "Invalid candidate format: " << error.message()
							          << std::endl;
							break;
						case webrtc::RTCErrorType::INVALID_STATE:
							std::cerr << "Call AddIceCandidate after SetRemoteDescription!"
							          << std::endl;
							break;
						default:
							std::cerr << "Unexpected error: " << error.message() << std::endl;
						}
					}
				});
			} catch (const std::exception& e) {
				std::cerr << e.what() << '\n';
			}
		}
		pending_candidates_.clear();
	}

	this->restarting_ice_.store(false);

	return;
}

const std::string PeerTransport::GetLocalDescription() {
	std::lock_guard<std::mutex> guard(pc_lock_);
	auto desc = this->pc_->local_description();
	std::string sdp;

	desc->ToString(&sdp);

	return sdp;
}

const std::string PeerTransport::GetRemoteDescription() {
	std::lock_guard<std::mutex> guard(pc_lock_);
	auto desc = this->pc_->remote_description();
	if (!desc) {
		return "";
	}
	std::string sdp;

	desc->ToString(&sdp);

	return sdp;
}

const std::string PeerTransport::GetCurrentLocalDescription() {
	auto desc = this->pc_->current_local_description();
	if (!desc) {
		return "";
	}
	std::string sdp;
	desc->ToString(&sdp);
	return sdp;
}

const std::string PeerTransport::GetCurrentRemoteDescription() {
	std::lock_guard<std::mutex> guard(pc_lock_);
	if (!this->pc_) {
		return "";
	}
	try {
		const webrtc::SessionDescriptionInterface* desc = this->pc_->current_remote_description();
		if (!desc) {
			return "";
		}
		std::string sdp;
		desc->ToString(&sdp);
		return sdp;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
	}
	return "";
}

const std::string PeerTransport::GetPendingLocalDescription() {
	std::lock_guard<std::mutex> guard(pc_lock_);
	auto desc = this->pc_->pending_local_description();
	if (!desc) {
		return "";
	}
	std::string sdp;
	desc->ToString(&sdp);
	return sdp;
}

const std::string PeerTransport::GetPendingRemoteDescription() {
	std::lock_guard<std::mutex> guard(pc_lock_);
	auto desc = this->pc_->pending_remote_description();
	if (!desc) {
		return "";
	}
	std::string sdp;
	desc->ToString(&sdp);
	return sdp;
}

const webrtc::PeerConnectionInterface::PeerConnectionState PeerTransport::GetConnectionState() {
	// std::lock_guard<std::mutex> guard(pc_lock_);
	if (this->pc_) {
		return this->pc_->peer_connection_state();
	}
	return webrtc::PeerConnectionInterface::PeerConnectionState::kClosed;
}

std::vector<rtc::scoped_refptr<webrtc::RtpTransceiverInterface>>
PeerTransport::GetTransceivers() const {
	std::lock_guard<std::mutex> guard(pc_lock_);
	return this->pc_->GetTransceivers();
}

rtc::scoped_refptr<webrtc::RtpTransceiverInterface>
PeerTransport::AddTransceiver(cricket::MediaType mediaType) {
	std::lock_guard<std::mutex> guard(pc_lock_);
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

	std::lock_guard<std::mutex> guard(pc_lock_);
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
	auto has_remot_decs = this->GetCurrentRemoteDescription().empty();
	if (this->pc_ == nullptr || (has_remot_decs && !this->restarting_ice_.load())) {
		std::lock_guard<std::mutex> guard(pending_candidates_lock_);
		pending_candidates_.push_back(candidate_json_str);
		return;
	}
	std::lock_guard<std::mutex> guard(pc_lock_);
	try {
		// signaling_thread_->PostTask([this, candidate_json_str]() {
		// });

		auto candidate = deserialize_ice_candidate(candidate_json_str);
		this->pc_->AddIceCandidate(std::move(candidate), [](webrtc::RTCError error) {
			if (error.ok()) {
				std::cout << "ICE Candidate added successfully." << std::endl;
			} else {
				switch (error.type()) {
				case webrtc::RTCErrorType::INVALID_PARAMETER:
					std::cerr << "Invalid candidate format: " << error.message() << std::endl;
					break;
				case webrtc::RTCErrorType::INVALID_STATE:
					std::cerr << "Call AddIceCandidate after SetRemoteDescription!" << std::endl;
					break;
				default:
					std::cerr << "Unexpected error: " << error.message() << std::endl;
				}
			}
		});
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
	}
	return;
}

bool PeerTransport::Negotiate() {
	// May throw.
	webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
	options.offer_to_receive_video = 0;
	options.offer_to_receive_audio = 0;
	options.voice_activity_detection = true;
	options.use_rtp_mux = true;
	options.use_obsolete_sctp_sdp = true;
	options.ice_restart = true;

	try {
		createAndSendPublisherOffer(options);
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return false;
	}

	return true;
}

bool PeerTransport::TestFlushIceCandidate() {
	std::lock_guard<std::mutex> guard(pending_candidates_lock_);
	for (auto& candidate_str : pending_candidates_) {
		try {
			auto candidate = deserialize_ice_candidate(candidate_str);
			this->pc_->AddIceCandidate(std::move(candidate), [](webrtc::RTCError error) {
				if (error.ok()) {
					std::cout << "ICE Candidate added successfully." << std::endl;
				} else {
					switch (error.type()) {
					case webrtc::RTCErrorType::INVALID_PARAMETER:
						std::cerr << "Invalid candidate format: " << error.message() << std::endl;
						break;
					case webrtc::RTCErrorType::INVALID_STATE:
						std::cerr << "Call AddIceCandidate after SetRemoteDescription!"
						          << std::endl;
						break;
					default:
						std::cerr << "Unexpected error: " << error.message() << std::endl;
					}
				}
			});
		} catch (const std::exception& e) {
			std::cerr << e.what() << '\n';
		}
	}
	pending_candidates_.clear();
	return !pending_candidates_.empty();
}

bool PeerTransport::create_peer_connection() {
	std::lock_guard<std::mutex> guard(pc_lock_);
	webrtc::PeerConnectionDependencies deps{this};
	auto result = this->pc_factory_->CreatePeerConnectionOrError(rtc_config_, std::move(deps));
	if (!result.ok()) {
		std::cerr << "Failed to create peer connection: " << result.error().message() << std::endl;
		return false;
	}
	this->pc_ = result.value();
	return true;
}

void PeerTransport::createAndSendPublisherOffer(
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions& options) {
	if (options.ice_restart) {
		this->restarting_ice_.store(true);
	}

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
		this->listener_->OnOffer(target_, std::move(new_desc));
	}
	return;
}

/* PeerConnection observer */

/**
 * Triggered when the SignalingState changed.
 */
void PeerTransport::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState newState) {
	std::cout << "OnSignalingChange: " << "target=" << target2String[target_]
	          << ",state=" << signalingState2String[newState] << std::endl;
	if (this->listener_) {
		this->listener_->OnSignalingChange(target_, newState);
	}
	return;
}

/**
 * Triggered when the ConnectionState changed.
 */
void PeerTransport::OnConnectionChange(
    webrtc::PeerConnectionInterface::PeerConnectionState new_state) {
	std::cout << "OnConnectionChange: " << "target=" << target2String[target_]
	          << ",state=" << peerConnectionState2String[new_state] << std::endl;
	if (this->listener_) {
		this->listener_->OnConnectionChange(target_, new_state);
	}
}

/**
 * Triggered when media is received on a new stream from remote peer.
 */
void PeerTransport::OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
	if (this->listener_) {
		this->listener_->OnAddStream(target_, stream);
	}
}

/**
 * Triggered when a remote peer closes a stream.
 */
void PeerTransport::OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
	if (this->listener_) {
		this->listener_->OnRemoveStream(target_, stream);
	}
}

/**
 * Triggered when a remote peer opens a data channel.
 */
void PeerTransport::OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel) {
	std::cout << "OnDataChannel: " << "target=" << target2String[target_] << std::endl;
	if (this->listener_) {
		this->listener_->OnDataChannel(target_, dataChannel);
	}
}

/**
 * Triggered when renegotiation is needed. For example, an ICE restart has begun.
 */
void PeerTransport::OnRenegotiationNeeded() {
	if (this->listener_) {
		this->listener_->OnRenegotiationNeeded(target_);
	}
}

/**
 * Triggered any time the IceConnectionState changes.
 *
 * Note that our ICE states lag behind the standard slightly. The most
 * notable differences include the fact that "failed" occurs after 15
 * seconds, not 30, and this actually represents a combination ICE + DTLS
 * state, so it may be "failed" if DTLS fails while ICE succeeds.
 */
void PeerTransport::OnIceConnectionChange(
    webrtc::PeerConnectionInterface::IceConnectionState newState) {
	std::cout << "OnIceConnectionChange: " << "target=" << target2String[target_]
	          << ",state=" << iceConnectionState2String[newState] << std::endl;

	if (this->listener_) {
		this->listener_->OnIceConnectionChange(target_, newState);
	}
	return;
}

/**
 * Triggered any time the IceGatheringState changes.
 */
void PeerTransport::OnIceGatheringChange(
    webrtc::PeerConnectionInterface::IceGatheringState newState) {
	std::cout << "OnIceGatheringChange: " << "target=" << target2String[target_]
	          << ",state=" << iceGatheringState2String[newState] << std::endl;

	if (this->listener_) {
		this->listener_->OnIceGatheringChange(target_, newState);
	}
	return;
}

/**
 * Triggered when a new ICE candidate has been gathered.
 */
void PeerTransport::OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
	RTC_LOG(LS_INFO) << __FUNCTION__ << " " << candidate->sdp_mline_index();
	if (this->listener_) {
		this->listener_->OnIceCandidate(target_, candidate);
	}
}

/**
 * Triggered when the ICE candidates have been removed.
 */
void PeerTransport::OnIceCandidatesRemoved(const std::vector<cricket::Candidate>& candidates) {
	std::cout << "OnIceCandidatesRemoved: " << "target=" << target2String[target_] << std::endl;
	if (this->listener_) {
		this->listener_->OnIceCandidatesRemoved(target_, candidates);
	}
}

/**
 * Triggered when the ICE connection receiving status changes.
 */
void PeerTransport::OnIceConnectionReceivingChange(bool receiving) {
	std::cout << "OnIceConnectionReceivingChange: " << "target=" << target2String[target_]
	          << std::endl;

	if (this->listener_) {
		this->listener_->OnIceConnectionReceivingChange(target_, receiving);
	}
}

/**
 * Triggered when the ICE connection receiving error.
 */
void PeerTransport::OnIceCandidateError(const std::string& address, int port,
                                        const std::string& url, int error_code,
                                        const std::string& error_text) {
	std::cout << "OnIceCandidateError: " << "target=" << target2String[target_]
	          << ",address=" << address << ",port=" << port << ",url=" << url
	          << ",error_code=" << error_code << ",error_text=" << error_text << std::endl;

	if (this->listener_) {
		this->listener_->OnIceCandidateError(target_, address, port, url, error_code, error_text);
	}
}
/**
 * Triggered when a receiver and its track are created.
 *
 * Note: This is called with both Plan B and Unified Plan semantics. Unified
 * Plan users should prefer OnTrack, OnAddTrack is only called as backwards
 * compatibility (and is called in the exact same situations as OnTrack).
 */
void PeerTransport::OnAddTrack(
    rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
    const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) {
	std::cout << "OnAddTrack: " << "target=" << target2String[target_] << std::endl;
	if (this->listener_) {
		this->listener_->OnAddTrack(target_, receiver, streams);
	}
}

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
void PeerTransport::OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
	std::cout << "OnTrack: " << "target=" << target2String[target_] << std::endl;
	if (this->listener_) {
		this->listener_->OnTrack(target_, transceiver);
	}
}

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
void PeerTransport::OnRemoveTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) {
	std::cout << "OnRemoveTrack: " << "target=" << target2String[target_] << std::endl;
	if (this->listener_) {
		this->listener_->OnRemoveTrack(target_, receiver);
	}
}

/**
 * Triggered when an interesting usage is detected by WebRTC.
 *
 * An appropriate action is to add information about the context of the
 * PeerConnection and write the event to some kind of "interesting events"
 * log function.
 * The heuristics for defining what constitutes "interesting" are
 * implementation-defined.
 */
void PeerTransport::OnInterestingUsage(int usagePattern) {
	std::cout << "OnInterestingUsage: " << "target=" << target2String[target_] << std::endl;
	if (this->listener_) {
		this->listener_->OnInterestingUsage(target_, usagePattern);
	}
}

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
