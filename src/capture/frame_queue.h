/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace livekit::capture {

struct OwnedBgraFrame {
	std::vector<std::uint8_t> data;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::uint32_t row_stride_bytes = 0;
	std::int64_t timestamp_us = 0;
	std::uint16_t rotation_degrees = 0;
	bool mirrored = false;
};

// Copies callback-owned BGRA frames and processes only the newest pending frame on a worker thread.
// Stop() prevents further delivery and joins the worker before returning.
class LatestVideoFrameQueue {
public:
	using FrameHandler = std::function<void(const OwnedBgraFrame& frame)>;

	explicit LatestVideoFrameQueue(FrameHandler handler);
	~LatestVideoFrameQueue();

	LatestVideoFrameQueue(const LatestVideoFrameQueue&) = delete;
	LatestVideoFrameQueue& operator=(const LatestVideoFrameQueue&) = delete;

	bool Start();
	void Stop() noexcept;
	bool IsRunning() const noexcept;
	bool Push(const std::uint8_t* data, std::uint32_t width, std::uint32_t height,
	          std::uint32_t row_stride_bytes, std::int64_t timestamp_us,
	          std::uint16_t rotation_degrees = 0, bool mirrored = false) noexcept;

private:
	void Run() noexcept;

	FrameHandler handler_;
	mutable std::mutex lifecycle_mutex_;
	mutable std::mutex mutex_;
	std::condition_variable condition_;
	std::optional<OwnedBgraFrame> pending_frame_;
	std::thread worker_;
	std::atomic_bool running_{false};
	bool stopping_ = false;
};

} // namespace livekit::capture
