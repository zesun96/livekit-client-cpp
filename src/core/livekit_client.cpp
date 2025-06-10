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
#include <sstream>

namespace livekit {
namespace core {

bool Init() {
	bool ret = true;

	std::cout << "livekit_client version: " << Version() << std::endl;

	ret = rtc::InitializeSSL();
	RTC_CHECK(ret) << "Failed to CleanupSSL()";
	ret = rtc::InitRandom(rtc::Time());
	rtc::LogMessage::LogToDebug(rtc::LS_ERROR);
#ifdef WEBRTC_WIN
	WSADATA data;
	WSAStartup(MAKEWORD(1, 0), &data);
#endif

	return ret;
}

bool Destroy() {
	bool ret = true;
	ret = rtc::CleanupSSL();
	RTC_CHECK(ret) << "Failed to CleanupSSL()";
#ifdef WEBRTC_WIN
	WSACleanup();
#endif
	return ret;
}

std::string Version() {

	std::stringstream ss;

	ss << LKC_CORE_VERSION_MAJOR << "." << LKC_CORE_VERSION_MINOR << "." << LKC_CORE_VERSION_PATCH;

	return ss.str();
}

} // namespace core
} // namespace livekit
