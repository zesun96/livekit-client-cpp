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

#include "debouncer.h"

#include <stdexcept>

namespace livekit {
namespace core {

Debouncer::Debouncer(std::chrono::milliseconds interval) : interval_(interval) {
	if (interval < std::chrono::milliseconds::zero()) {
		throw std::invalid_argument("Debounce interval must not be negative");
	}
}

bool Debouncer::lock() {
	std::lock_guard<std::mutex> guard(mutex_);
	const auto now = std::chrono::steady_clock::now();
	if (last_time_ && now - *last_time_ < interval_) {
		return false;
	}

	last_time_ = now;
	return true;
}

std::unique_ptr<Debouncer> Debouncer::Create(std::chrono::milliseconds interval) {
	return std::unique_ptr<Debouncer>(new Debouncer(interval));
}

} // namespace core
} // namespace livekit
