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

#pragma once

#ifndef _LKC_CORE_DETAIL_PEER_TRANSPORT_FACTORY_H_
#define _LKC_CORE_DETAIL_PEER_TRANSPORT_FACTORY_H_

#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#include "api/task_queue/task_queue_factory.h"
#include "rtc_base/logging.h"
#include "rtc_base/physical_socket_server.h"
#include "rtc_base/ssl_adapter.h"

#ifdef WEBRTC_WIN
#include "rtc_base/win32_socket_init.h"
#endif

namespace livekit {
namespace core {

class PeerTransportFactory {
public:
	static std::shared_ptr<PeerTransportFactory> Create();

	explicit PeerTransportFactory();
	~PeerTransportFactory();

	webrtc::PeerConnectionFactoryInterface* GetPeerConnectFactory() { return peer_factory_.get(); }

	rtc::Thread* network_thread() const;
	rtc::Thread* worker_thread() const;
	rtc::Thread* signaling_thread() const;

private:
	// rtc::scoped_refptr<AudioDevice> audio_device_;
	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_factory_;
	webrtc::TaskQueueFactory* task_queue_factory_;

	std::unique_ptr<rtc::Thread> network_thread_;
	std::unique_ptr<rtc::Thread> worker_thread_;
	std::unique_ptr<rtc::Thread> signaling_thread_;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_PEER_TRANSPORT_FACTORY_H_