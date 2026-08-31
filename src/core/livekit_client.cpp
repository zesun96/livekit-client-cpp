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

#include "livekit/core/livekit_client.h"
#include "detail/logging.h"
#include "detail/tracing.h"
#include "livekit/core/version.h"

#include <rtc_base/crypto_random.h>
#include <rtc_base/ssl_adapter.h>

#include <mutex>

namespace {
std::mutex initialization_mutex;
std::size_t initialization_count = 0;
} // namespace

namespace livekit {
namespace core {

bool Init() {
	LKC_TRACE_SPAN(TraceCategory::Lifecycle, "runtime.init");
	std::lock_guard<std::mutex> guard(initialization_mutex);
	if (initialization_count != 0) {
		++initialization_count;
		LKC_LOG_DEBUG << "runtime reference acquired, count=" << initialization_count;
		return true;
	}

	if (!webrtc::InitializeSSL()) {
		LKC_LOG_ERROR << "failed to initialize WebRTC SSL";
		return false;
	}
	webrtc::SetDefaultRandomGenerator();
	detail::StartBackendLogging();

#ifdef WEBRTC_WIN
	WSADATA data;
	if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
		LKC_LOG_ERROR << "failed to initialize Winsock";
		detail::StopBackendLogging();
		webrtc::CleanupSSL();
		return false;
	}
#endif

	initialization_count = 1;
	LKC_LOG_INFO << "LiveKit client " << Version() << " initialized";
	return true;
}

bool Destroy() {
	LKC_TRACE_SPAN(TraceCategory::Lifecycle, "runtime.shutdown");
	std::lock_guard<std::mutex> guard(initialization_mutex);
	if (initialization_count == 0) {
		return true;
	}
	if (--initialization_count != 0) {
		LKC_LOG_DEBUG << "runtime reference released, count=" << initialization_count;
		return true;
	}
#ifdef WEBRTC_WIN
	WSACleanup();
#endif
	const bool cleaned = webrtc::CleanupSSL();
	if (cleaned) {
		LKC_LOG_INFO << "LiveKit client shutdown complete";
	} else {
		LKC_LOG_ERROR << "failed to clean up WebRTC SSL";
	}
	detail::StopBackendLogging();
	return cleaned;
}

std::string Version() {
	return std::to_string(LKC_CORE_VERSION_MAJOR) + "." + std::to_string(LKC_CORE_VERSION_MINOR) +
	       "." + std::to_string(LKC_CORE_VERSION_PATCH);
}

} // namespace core
} // namespace livekit
