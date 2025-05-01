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

#include "livekit/core/livekit_client_test.h"
#include "detail/peer_transport.h"
#include "detail/signal_client.h"

#include <api/peer_connection_interface.h>

#if defined(WEBRTC_WIN)
#include <rtc_base/win32_socket_init.h>
#endif

#include <nlohmann/json.hpp>

#include <any>
#include <functional>
#include <iostream>
#include <memory>

namespace livekit {
namespace core {
static bool make_rtc_config_join2() {
	// webrtc::PeerConnectionInterface::RTCConfigurationTest111 test11{};
	webrtc::PeerConnectionInterface::RTCConfiguration rtc_config(
	    webrtc::PeerConnectionInterface::RTCConfigurationType::kSafe);
	std::string str;
	rtc_config.turn_logging_id = str;

	return true;
}

void TestWebrtc() {
	auto cpu = make_rtc_config_join2();
	webrtc::PeerConnectionInterface::RTCConfigurationTest111 test11{};
	return;
}

bool Test() {
	auto option = SignalOptions();
	auto signal_client = SignalClient::Create("ws://localhost:8080/ws", "aaa", option);
	signal_client->Connect();
	while (true) {
	}
	return true;
}

class EventEmitter {
public:
	using Listener = std::function<void(const std::any&)>;

	void on(const std::string& event, Listener listener) {
		listeners_[event].emplace_back(std::move(listener));
	}

	template <typename T> void emit(const std::string& event, T&& data) {
		using StorageType = std::decay_t<T>;

		static_assert(std::is_constructible_v<std::any, StorageType>,
		              "Cannot construct std::any from this type");

		static_assert(!std::is_same_v<StorageType, std::any>,
		              "Direct emission of std::any is prohibited");

		if (auto it = listeners_.find(event); it != listeners_.end()) {
			std::any wrapped_data = std::forward<StorageType>(data);
			for (const auto& listener : it->second) {
				listener(wrapped_data);
			}
		}
	}

	// 无数据版本保持独立
	void emit(const std::string& event) {
		if (auto it = listeners_.find(event); it != listeners_.end()) {
			for (const auto& listener : it->second) {
				listener(std::any{});
			}
		}
	}

private:
	std::unordered_map<std::string, std::vector<Listener>> listeners_;
};

static EventEmitter bob_emitter;
static EventEmitter alice_emitter;

template <typename T> struct EventValidType {
	int id;
	T Value_;
};

class TestPeerTransportListener : public PeerTransport::PeerTransportListener {
public:
	virtual ~TestPeerTransportListener() = default;

	virtual void OnOffer(PeerTransport::Target target,
	                     std::unique_ptr<webrtc::SessionDescriptionInterface> offer) override {
		// emitter->emit(
		//     "offer", EventValidType <webrtc::SessionDescriptionInterface*>{1, offer.get()});

		emitter->emit("offer", offer.get());
	}

	virtual void
	OnSignalingChange(PeerTransport::Target target,
	                  webrtc::PeerConnectionInterface::SignalingState newState) override {
		emitter->emit("signaling_change", newState);
	}
	virtual void
	OnConnectionChange(PeerTransport::Target target,
	                   webrtc::PeerConnectionInterface::PeerConnectionState new_state) override {
		emitter->emit("connection_change", new_state);
	}
	virtual void OnAddStream(PeerTransport::Target target,
	                         rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override {
		emitter->emit("add_stream", stream);
	}
	virtual void OnRemoveStream(PeerTransport::Target target,
	                            rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override {
		emitter->emit("remove_stream", stream);
	}
	virtual void
	OnDataChannel(PeerTransport::Target target,
	              rtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel) override {
		emitter->emit("data_channel", dataChannel);
	}
	virtual void OnRenegotiationNeeded(PeerTransport::Target target) override {
		emitter->emit("renegotiation_needed");
	}
	virtual void
	OnIceConnectionChange(PeerTransport::Target target,
	                      webrtc::PeerConnectionInterface::IceConnectionState newState) override {
		emitter->emit("ice_connection_change", newState);
	}
	virtual void
	OnIceGatheringChange(PeerTransport::Target target,
	                     webrtc::PeerConnectionInterface::IceGatheringState newState) override {
		emitter->emit("ice_gathering_change", newState);
	}
	virtual void OnIceCandidate(PeerTransport::Target target,
	                            const webrtc::IceCandidateInterface* candidate) override {
		emitter->emit("ice_candidate", candidate);
	}
	virtual void
	OnIceCandidatesRemoved(PeerTransport::Target target,
	                       const std::vector<cricket::Candidate>& candidates) override {
		emitter->emit("ice_candidates_removed", std::cref(candidates));
	}
	virtual void OnIceConnectionReceivingChange(PeerTransport::Target target,
	                                            bool receiving) override {
		emitter->emit("ice_connection_receiving_change", receiving);
	}
	virtual void OnIceCandidateError(PeerTransport::Target target, const std::string& address,
	                                 int port, const std::string& url, int error_code,
	                                 const std::string& error_text) override {
		emitter->emit("ice_candidate_error",
		              std::make_tuple(address, port, url, error_code, error_text));
	}
	virtual void OnAddTrack(
	    PeerTransport::Target target, rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
	    const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) override {
		emitter->emit("add_track", std::make_tuple(target, receiver, streams));
	}
	virtual void OnTrack(PeerTransport::Target target,
	                     rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override {
		emitter->emit("track", std::make_tuple(target, transceiver));
	}
	virtual void OnRemoveTrack(PeerTransport::Target target,
	                           rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {
		emitter->emit("remove_track", std::make_tuple(target, receiver));
	}
	virtual void OnInterestingUsage(PeerTransport::Target target, int usagePattern) override {
		emitter->emit("interesting_usage", std::make_tuple(target, usagePattern));
	}

	EventEmitter* emitter = nullptr;
};

class DataChannelObserverListener : public webrtc::DataChannelObserver {
public:
	DataChannelObserverListener() = default;
	virtual ~DataChannelObserverListener() = default;

	void OnStateChange() override {
		// emitter->emit("state_change");
		return;
	}

	void OnMessage(const webrtc::DataBuffer& buffer) override {
		std::string message(buffer.data.data<char>(), buffer.data.size());
		std::cout << "DataChannel message: " << message << std::endl;
		emitter->emit("message", message);
	}
	void OnBufferedAmountChange(uint64_t sent_data_size) override {}
	EventEmitter* emitter = nullptr;
};

bool TestPeerConntion() {
#if defined(WEBRTC_WIN)
	std::unique_ptr<rtc::WinsockInitializer> winsock_init_ =
	    std::make_unique<rtc::WinsockInitializer>();
#endif

	webrtc::PeerConnectionInterface::RTCConfiguration rtc_config;
	webrtc::PeerConnectionInterface::IceServer rtc_ice_server;
	rtc_ice_server.uri = "stun:stun1.l.google.com:19302";
	rtc_config.servers.push_back(rtc_ice_server);
	rtc_config.continual_gathering_policy =
	    webrtc::PeerConnectionInterface::ContinualGatheringPolicy::GATHER_ONCE;
	// Set ICE transport type
	rtc_config.type = webrtc::PeerConnectionInterface::IceTransportsType::kAll;

	TestPeerTransportListener bob_listener;
	bob_listener.emitter = &bob_emitter;
	auto bob =
	    std::make_unique<PeerTransport>(PeerTransport::Target::PUBLISHER, rtc_config, nullptr);
	bob->AddPeerTransportListener(&bob_listener);
	TestPeerTransportListener alice_listener;
	alice_listener.emitter = &alice_emitter;
	auto alice =
	    std::make_unique<PeerTransport>(PeerTransport::Target::SUBSCRIBER, rtc_config, nullptr);
	alice->AddPeerTransportListener(&alice_listener);

	bob_emitter.on("ice_candidate", [&](const std::any& candidate) {
		std::cout << "bob got ice candidate" << std::endl;
		auto candidate_ptr = std::any_cast<const webrtc::IceCandidateInterface*>(candidate);
		std::string candidate_str;
		candidate_ptr->ToString(&candidate_str);
		nlohmann::json candidate_json;
		candidate_json["candidate"] = candidate_str;
		candidate_json["sdpMid"] = candidate_ptr->sdp_mid();
		candidate_json["sdpMLineIndex"] = candidate_ptr->sdp_mline_index();
		alice->AddIceCandidate(candidate_json.dump());
	});

	alice_emitter.on("ice_candidate", [&](const std::any& candidate) {
		std::cout << "alice got ice candidate" << std::endl;
		auto candidate_ptr = std::any_cast<const webrtc::IceCandidateInterface*>(candidate);
		std::string candidate_str;
		candidate_ptr->ToString(&candidate_str);
		nlohmann::json candidate_json;
		candidate_json["candidate"] = candidate_str;
		candidate_json["sdpMid"] = candidate_ptr->sdp_mid();
		candidate_json["sdpMLineIndex"] = candidate_ptr->sdp_mline_index();
		bob->AddIceCandidate(candidate_json.dump());
	});
	alice_emitter.on("data_channel", [&](const std::any& dataChannel) {
		std::cout << "alice got data channel" << std::endl;
		auto dataChannel_ptr =
		    std::any_cast<rtc::scoped_refptr<webrtc::DataChannelInterface>>(dataChannel);
	});
	alice_emitter.on("message", [&](const std::any& message) {
		std::cout << "alice got message" << std::endl;
		auto& message_ptr = std::any_cast<const std::string&>(message);
		std::cout << "alice got message: " << message_ptr << std::endl;
	});

	if (!bob->Init()) {
		return false;
	}
	if (!alice->Init()) {
		return false;
	}

	struct webrtc::DataChannelInit reliable_init;
	reliable_init.ordered = true;
	reliable_init.reliable = true;
	auto bob_reliable_dc = bob->CreateDataChannel("_reliable", &reliable_init);
	auto ob_buffered_amount = bob_reliable_dc->buffered_amount();

	auto alice_reliable_dc = alice->CreateDataChannel("_reliable", &reliable_init);
	auto alice_buffered_amount = alice_reliable_dc->buffered_amount();
	DataChannelObserverListener alice_dc_observer;
	alice_dc_observer.emitter = &alice_emitter;
	alice_reliable_dc->RegisterObserver(&alice_dc_observer);

	webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
	options.offer_to_receive_video = 0;
	options.offer_to_receive_audio = 0;
	options.voice_activity_detection = true;
	options.use_rtp_mux = true;
	options.use_obsolete_sctp_sdp = true;
	options.ice_restart = true;
	auto offer = bob->CreateOffer(options);

	std::cout << "bob got offer: " << offer << std::endl;

	std::unique_ptr<webrtc::SessionDescriptionInterface> offer_desc =
	    ConvertSdp(webrtc::SdpType::kOffer, offer);

	bob->SetLocalDescription(std::move(offer_desc));

	std::unique_ptr<webrtc::SessionDescriptionInterface> offer_new_desc =
	    ConvertSdp(webrtc::SdpType::kOffer, offer);

	alice->SetRemoteDescription(std::move(offer_new_desc));

	webrtc::PeerConnectionInterface::RTCOfferAnswerOptions alice_options;
	options.offer_to_receive_video = true;
	options.offer_to_receive_audio = true;
	auto answer = alice->CreateAnswer(alice_options);

	std::cout << "alice got answer: " << answer << std::endl;

	std::unique_ptr<webrtc::SessionDescriptionInterface> answer_desc =
	    ConvertSdp(webrtc::SdpType::kAnswer, answer);

	alice->SetLocalDescription(std::move(answer_desc));

	std::unique_ptr<webrtc::SessionDescriptionInterface> answer_new_desc =
	    ConvertSdp(webrtc::SdpType::kAnswer, answer);
	bob->SetRemoteDescription(std::move(answer_new_desc));

	auto timer_ = std::make_shared<Timer>();
	timer_->SetTimeout(
	    [=]() {
		    std::string data("This is a test");
		    webrtc::DataBuffer buffer(data);
		    bool ret = bob_reliable_dc->Send(buffer);

		    std::cout << "ret:" << ret << std::endl;
	    },
	    12 * 1000);

	while (true) {
	}

	return true;
}

} // namespace core
} // namespace livekit
