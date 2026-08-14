/**
 *
 * Copyright (c) 2024 sunze
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

#pragma once

#ifndef _LKC_CORE_OPTION_RECONNECT_POLICY_H_
#define _LKC_CORE_OPTION_RECONNECT_POLICY_H_

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace livekit {
namespace core {

enum class ReconnectReason {
	Unknown,
	SignalDisconnected,
	MediaFailure,
};

struct ReconnectContext {
	uint32_t retry_count = 0;
	std::chrono::milliseconds elapsed{0};
	ReconnectReason reason = ReconnectReason::Unknown;
	std::string server_url;
};

class ReconnectPolicy {
public:
	virtual ~ReconnectPolicy() = default;

	// Return the delay before this full-reconnect attempt, or nullopt to stop recovery. The SDK
	// invokes policies on its recovery thread; implementations must not block that thread.
	virtual std::optional<std::chrono::milliseconds>
	NextRetryDelay(const ReconnectContext& context) = 0;
};

class DefaultReconnectPolicy final : public ReconnectPolicy {
public:
	std::optional<std::chrono::milliseconds>
	NextRetryDelay(const ReconnectContext& context) override;
};

std::shared_ptr<ReconnectPolicy> CreateDefaultReconnectPolicy();

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_OPTION_RECONNECT_POLICY_H_
