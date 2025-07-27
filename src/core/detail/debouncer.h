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

#ifndef _LKC_CORE_CONVERTED_DEBOUNCER_H_
#define _LKC_CORE_CONVERTED_DEBOUNCER_H_

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>

namespace livekit {
namespace core {

class Debouncer {
public:
	static std::unique_ptr<Debouncer> Create(std::chrono::milliseconds interval);

	bool lock();

	Debouncer(const Debouncer&) = delete;
	Debouncer& operator=(const Debouncer&) = delete;
	Debouncer(Debouncer&&) = delete;
	Debouncer& operator=(Debouncer&&) = delete;

private:
	explicit Debouncer(std::chrono::milliseconds interval);

	std::chrono::milliseconds interval_;
	std::mutex mutex_;
	std::atomic<std::chrono::steady_clock::time_point> last_time_;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_CONVERTED_DEBOUNCER_H_