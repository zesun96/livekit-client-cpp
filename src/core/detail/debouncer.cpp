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

namespace livekit {
namespace core {

Debouncer::Debouncer(std::chrono::milliseconds interval)
    : interval_(interval), last_time_(std::chrono::steady_clock::time_point::min()) {}

bool Debouncer::lock() {
	auto now = std::chrono::steady_clock::now();
	auto last = last_time_.load(std::memory_order_relaxed);

	if (now - last < interval_) {
		return false;
	}

	std::lock_guard<std::mutex> guard(mutex_);
	if ((now - last_time_.load()) < interval_) {
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
