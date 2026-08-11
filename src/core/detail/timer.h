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

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace livekit {
namespace core {

class Timer : public std::enable_shared_from_this<Timer> {
public:
	~Timer() { Stop(); }

	template <typename Function> void SetTimeout(Function function, int delay_ms) {
		Start(std::move(function), delay_ms, false);
	}

	template <typename Function> void SetInterval(Function function, int interval_ms) {
		Start(std::move(function), interval_ms, true);
	}

	void Stop();

private:
	template <typename Function> void Start(Function function, int delay_ms, bool repeat) {
		if (delay_ms < 0) {
			throw std::invalid_argument("Timer delay must not be negative");
		}
		Stop();
		{
			std::lock_guard<std::mutex> guard(mutex_);
			active_ = true;
		}

		auto self = shared_from_this();
		worker_ = std::thread([self, function = std::move(function), delay_ms, repeat]() mutable {
			std::unique_lock<std::mutex> lock(self->mutex_);
			do {
				if (self->wake_.wait_for(lock, std::chrono::milliseconds(delay_ms),
				                         [self]() { return !self->active_; })) {
					return;
				}
				if (!repeat) {
					self->active_ = false;
				}
				lock.unlock();
				function();
				lock.lock();
			} while (self->active_ && repeat);
		});
	}

	std::mutex mutex_;
	std::condition_variable wake_;
	bool active_ = false;
	std::thread worker_;
};

inline void Timer::Stop() {
	{
		std::lock_guard<std::mutex> guard(mutex_);
		active_ = false;
	}
	wake_.notify_all();
	if (!worker_.joinable()) {
		return;
	}
	if (worker_.get_id() == std::this_thread::get_id()) {
		worker_.detach();
	} else {
		worker_.join();
	}
}

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DETAIL_TIMER_H_
