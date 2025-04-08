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

#ifndef _LKC_CORE_DETAIL_TIMER_H_
#define _LKC_CORE_DETAIL_TIMER_H_

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

namespace livekit {
namespace core {

class Timer : public std::enable_shared_from_this<Timer> {
	std::atomic<bool> active{true};

public:
	template <typename Function> void SetTimeout(Function function, int delay) {
		active.store(true);
		auto self = shared_from_this();
		std::thread t([self, function, delay]() {
			if (!self->active.load())
				return;
			std::this_thread::sleep_for(std::chrono::milliseconds(delay));
			if (!self->active.load())
				return;
			function();
		});
		t.detach();
	}

	template <typename Function> void SetInterval(Function function, int interval) {
		active.store(true);
		auto self = shared_from_this();
		std::thread t([self, function, interval]() {
			while (self->active.load()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(interval));
				if (!self->active.load())
					return;
				function();
			}
		});
		t.detach();
	}

	void Stop();
};

inline void Timer::Stop() { active.store(false); }

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_TIMER_H_
