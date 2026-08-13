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

#include "api/audio/builtin_audio_processing_builder.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/create_peerconnection_factory.h"
#include "api/environment/environment_factory.h"
#include "api/peer_connection_interface.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/video_codecs/video_decoder_factory_template.h"
#include "api/video_codecs/video_decoder_factory_template_dav1d_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_open_h264_adapter.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory_template_libaom_av1_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_open_h264_adapter.h"
#include "rtc_base/thread.h"

#include <iostream>

namespace livekit {
namespace core {

PeerTransportFactory::PeerTransportFactory() {
	network_thread_ = webrtc::Thread::CreateWithSocketServer();
	network_thread_->SetName("network_thread", &network_thread_);
	network_thread_->Start();
	worker_thread_ = webrtc::Thread::Create();
	worker_thread_->SetName("worker_thread", &worker_thread_);
	worker_thread_->Start();
	signaling_thread_ = webrtc::Thread::Create();
	signaling_thread_->SetName("signaling_thread", &signaling_thread_);
	signaling_thread_->Start();

	task_queue_factory_ = webrtc::CreateDefaultTaskQueueFactory();
	audio_device_ = worker_thread_->BlockingCall(
	    [&] { return webrtc::make_ref_counted<AudioDevice>(task_queue_factory_.get()); });
	auto audio_processing =
	    webrtc::BuiltinAudioProcessingBuilder().Build(webrtc::CreateEnvironment());
	peer_factory_ = webrtc::CreatePeerConnectionFactory(
	    network_thread_.get(), worker_thread_.get(), signaling_thread_.get(), audio_device_,
	    webrtc::CreateBuiltinAudioEncoderFactory(), webrtc::CreateBuiltinAudioDecoderFactory(),
	    std::make_unique<webrtc::VideoEncoderFactoryTemplate<
	        webrtc::LibvpxVp8EncoderTemplateAdapter, webrtc::LibvpxVp9EncoderTemplateAdapter,
	        webrtc::OpenH264EncoderTemplateAdapter, webrtc::LibaomAv1EncoderTemplateAdapter>>(),
	    std::make_unique<webrtc::VideoDecoderFactoryTemplate<
	        webrtc::LibvpxVp8DecoderTemplateAdapter, webrtc::LibvpxVp9DecoderTemplateAdapter,
	        webrtc::OpenH264DecoderTemplateAdapter, webrtc::Dav1dDecoderTemplateAdapter>>(),
	    nullptr, std::move(audio_processing));
}

PeerTransportFactory::~PeerTransportFactory() {
	std::cout << "PeerTransportFactory::~PeerTransportFactory()" << std::endl;
	peer_factory_ = nullptr;
	audio_device_ = nullptr;
	worker_thread_->Stop();
	signaling_thread_->Stop();
	network_thread_->Stop();
}

webrtc::Thread* PeerTransportFactory::network_thread() const { return network_thread_.get(); }

webrtc::Thread* PeerTransportFactory::worker_thread() const { return worker_thread_.get(); }

webrtc::Thread* PeerTransportFactory::signaling_thread() const { return signaling_thread_.get(); }

std::shared_ptr<PeerTransportFactory> PeerTransportFactory::Create() {
	return std::make_shared<PeerTransportFactory>();
}

} // namespace core
} // namespace livekit
