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
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/rtc_event_log/rtc_event_log_factory.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
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

	this->peer_factory_ = webrtc::CreatePeerConnectionFactory(
	    this->network_thread_.get(), this->worker_thread_.get(), this->signaling_thread_.get(),
	    nullptr /*default_adm*/, webrtc::CreateBuiltinAudioEncoderFactory(),
	    webrtc::CreateBuiltinAudioDecoderFactory(), nullptr, nullptr, nullptr /*audio_mixer*/,
	    nullptr /*audio_processing*/);
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