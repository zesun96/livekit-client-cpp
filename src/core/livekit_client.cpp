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
#include "version/version.h"

#include <rtc_base/helpers.h>
#include <rtc_base/logging.h>
#include <rtc_base/ssl_adapter.h>
#include <rtc_base/time_utils.h>

#include <iostream>
#include <mutex>

namespace {
std::mutex initialization_mutex;
std::size_t initialization_count = 0;
} // namespace

namespace livekit {
namespace core {

bool Init() {
	std::lock_guard<std::mutex> guard(initialization_mutex);
	if (initialization_count != 0) {
		++initialization_count;
		return true;
	}

	std::cout << "livekit_client version: " << Version() << std::endl;

	if (!rtc::InitializeSSL()) {
		return false;
	}
	if (!rtc::InitRandom(rtc::Time())) {
		rtc::CleanupSSL();
		return false;
	}
#if _DEBUG
	rtc::LogMessage::LogToDebug(rtc::LS_INFO);
	rtc::LogMessage::LogTimestamps(true);
#else
	rtc::LogMessage::LogToDebug(rtc::LS_ERROR);
#endif

#ifdef WEBRTC_WIN
	WSADATA data;
	if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
		rtc::CleanupSSL();
		return false;
	}
#endif

	initialization_count = 1;
	return true;
}

bool Destroy() {
	std::lock_guard<std::mutex> guard(initialization_mutex);
	if (initialization_count == 0) {
		return true;
	}
	if (--initialization_count != 0) {
		return true;
	}
#ifdef WEBRTC_WIN
	WSACleanup();
#endif
	return rtc::CleanupSSL();
}

std::string Version() {
	return std::to_string(LKC_CORE_VERSION_MAJOR) + "." + std::to_string(LKC_CORE_VERSION_MINOR) +
	       "." + std::to_string(LKC_CORE_VERSION_PATCH);
}

} // namespace core
} // namespace livekit
