/**
 *
 * Copyright (c) 2025 sunze
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

#include "peer_transport_factory.h"

#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
// #include "api/enable_media.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/rtc_event_log/rtc_event_log_factory.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "media/engine/webrtc_media_engine.h"
#include "modules/audio_device/include/audio_device.h"
#include "modules/audio_device/include/audio_device_factory.h"
#include "rtc_base/thread.h"
#include <api/create_peerconnection_factory.h>

#include <iostream>

namespace livekit {
namespace core {

PeerTransportFactory::PeerTransportFactory() {
	network_thread_ = rtc::Thread::CreateWithSocketServer();
	network_thread_->SetName("network_thread", &network_thread_);
	network_thread_->Start();
	worker_thread_ = rtc::Thread::Create();
	worker_thread_->SetName("worker_thread", &worker_thread_);
	worker_thread_->Start();
	signaling_thread_ = rtc::Thread::Create();
	signaling_thread_->SetName("signaling_thread", &signaling_thread_);
	signaling_thread_->Start();

	// this->peer_factory_ = webrtc::CreatePeerConnectionFactory(
	//     this->network_thread_.get(), this->worker_thread_.get(), this->signaling_thread_.get(),
	//     nullptr /*default_adm*/, webrtc::CreateBuiltinAudioEncoderFactory(),
	//     webrtc::CreateBuiltinAudioDecoderFactory(), nullptr, nullptr, nullptr /*audio_mixer*/,
	//     nullptr /*audio_processing*/);

	webrtc::PeerConnectionFactoryDependencies dependencies;
	dependencies.network_thread = network_thread_.get();
	dependencies.worker_thread = worker_thread_.get();
	dependencies.signaling_thread = signaling_thread_.get();
	dependencies.socket_factory = network_thread_->socketserver();
	dependencies.task_queue_factory = webrtc::CreateDefaultTaskQueueFactory();
	dependencies.event_log_factory =
	    std::make_unique<webrtc::RtcEventLogFactory>(dependencies.task_queue_factory.get());
	dependencies.call_factory = webrtc::CreateCallFactory();
	dependencies.trials = std::make_unique<webrtc::FieldTrialBasedConfig>();

	audio_device_ = worker_thread_->BlockingCall(
	    [&] { return rtc::make_ref_counted<AudioDevice>(dependencies.task_queue_factory.get()); });

	// dependencies.adm = audio_device_;

	cricket::MediaEngineDependencies media_deps;
	media_deps.task_queue_factory = dependencies.task_queue_factory.get();

	// media_deps.video_encoder_factory =
	// std::move(std::make_unique<livekit::VideoEncoderFactory>()); media_deps.video_decoder_factory
	// = std::move(std::make_unique<livekit::VideoDecoderFactory>());
	media_deps.adm = audio_device_;
	media_deps.audio_encoder_factory = webrtc::CreateBuiltinAudioEncoderFactory();
	media_deps.audio_decoder_factory = webrtc::CreateBuiltinAudioDecoderFactory();
	media_deps.audio_processing = webrtc::AudioProcessingBuilder().Create();
	media_deps.trials = dependencies.trials.get();

	dependencies.media_engine = cricket::CreateMediaEngine(std::move(media_deps));

	// webrtc::EnableMedia(dependencies);
	peer_factory_ = webrtc::CreateModularPeerConnectionFactory(std::move(dependencies));
}

PeerTransportFactory::~PeerTransportFactory() {
	std::cout << "PeerTransportFactory::~PeerTransportFactory()" << std::endl;
	worker_thread_->Stop();
	signaling_thread_->Stop();
	network_thread_->Stop();
}

rtc::Thread* PeerTransportFactory::network_thread() const { return network_thread_.get(); }

rtc::Thread* PeerTransportFactory::worker_thread() const { return worker_thread_.get(); }

rtc::Thread* PeerTransportFactory::signaling_thread() const { return signaling_thread_.get(); }

std::shared_ptr<PeerTransportFactory> PeerTransportFactory::Create() {
	return std::make_shared<PeerTransportFactory>();
}

} // namespace core
} // namespace livekit