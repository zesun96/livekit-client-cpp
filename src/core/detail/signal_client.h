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

#pragma once

#ifndef _LKC_CORE_DETAIL_SIGNAL_CLIENT_H_
#define _LKC_CORE_DETAIL_SIGNAL_CLIENT_H_

#include <memory>
#include <string>
#include <websocketclient.hpp>

namespace livekit {
namespace core {

struct SignalSdkOptions {
	std::string sdk;
	std::string sdk_version;
};

struct SignalClientOption {
	bool auto_subscribe;
	bool adaptive_stream;
	SignalSdkOptions sdk_options;
};

class SignalClient {
private:
public:
	static std::unique_ptr<SignalClient> Create(std::string url, std::string token,
	                                            SignalClientOption option);

	SignalClient(std::string url, std::string token, SignalClientOption option);
	~SignalClient();

private:
	bool Init();

private:
	std::string url_;
	std::string token_;
	SignalClientOption option_;
	std::unique_ptr<wsc::WebSocket> wsc_;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_SIGNAL_CLIENT_H_
