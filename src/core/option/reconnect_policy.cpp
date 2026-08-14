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

#include "livekit/core/option/reconnect_policy.h"

#include <algorithm>

namespace livekit {
namespace core {

std::optional<std::chrono::milliseconds>
DefaultReconnectPolicy::NextRetryDelay(const ReconnectContext& context) {
	constexpr uint64_t kBaseDelayMilliseconds = 300;
	constexpr uint64_t kMaximumDelayMilliseconds = 7'000;
	const auto retry = static_cast<uint64_t>(context.retry_count);
	const auto delay =
	    retry >= 5 ? kMaximumDelayMilliseconds : retry * retry * kBaseDelayMilliseconds;
	return std::chrono::milliseconds(delay);
}

std::shared_ptr<ReconnectPolicy> CreateDefaultReconnectPolicy() {
	return std::make_shared<DefaultReconnectPolicy>();
}

} // namespace core
} // namespace livekit
