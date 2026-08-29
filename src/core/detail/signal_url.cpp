/**
 * Copyright (c) 2026 sunze
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "signal_url.h"

#include "uri.h"

namespace livekit::core::detail {
namespace {

// Protocol 17 is the current signalling protocol. Optional additions such as encoded-frame packet
// trailers are negotiated independently through ClientInfo capabilities; this SDK does not
// advertise those capabilities until their corresponding public paths are implemented.
constexpr char kProtocolVersion[] = "17";
constexpr char kDefaultSdk[] = "cpp";
constexpr char kDefaultSdkVersion[] = "0.0.1";

void SetParameter(Url& url, const std::string& key, const std::string& value) {
	url.AppendQueryParameter(key, Url::Encode(value));
}

} // namespace

std::string BuildSignalUrl(const std::string& url, const std::string& token,
                           const SignalOptions& options) {
	Url request(url);
	SetParameter(request, "access_token", token);
	SetParameter(request, "auto_subscribe", options.auto_subscribe ? "1" : "0");
	SetParameter(request, "sdk",
	             options.sdk_options.sdk.empty() ? kDefaultSdk : options.sdk_options.sdk);
	SetParameter(request, "version",
	             options.sdk_options.sdk_version.empty() ? kDefaultSdkVersion
	                                                     : options.sdk_options.sdk_version);
	SetParameter(request, "protocol", kProtocolVersion);
	if (options.adaptive_stream) {
		SetParameter(request, "adaptive_stream", "1");
	}
	if (options.reconnect) {
		SetParameter(request, "reconnect", "1");
		if (!options.participant_sid.empty()) {
			SetParameter(request, "sid", options.participant_sid);
		}
		if (options.reconnect_reason != 0) {
			SetParameter(request, "reconnect_reason", std::to_string(options.reconnect_reason));
		}
	}
	return request.GetAbsoluteUrl();
}

} // namespace livekit::core::detail
