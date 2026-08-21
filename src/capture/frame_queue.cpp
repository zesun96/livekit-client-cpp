/**
 * Copyright (c) 2026 sunze
 * SPDX-License-Identifier: Apache-2.0
 */

#include "frame_queue.h"

#include <cstring>
#include <limits>
#include <utility>

namespace livekit::capture {

LatestVideoFrameQueue::LatestVideoFrameQueue(FrameHandler handler) : handler_(std::move(handler)) {}

LatestVideoFrameQueue::~LatestVideoFrameQueue() { Stop(); }

bool LatestVideoFrameQueue::Start() {
	std::lock_guard<std::mutex> lifecycle_guard(lifecycle_mutex_);
	if (running_.load()) {
		return true;
	}
	if (!handler_ || worker_.joinable()) {
		return false;
	}
	{
		std::lock_guard<std::mutex> guard(mutex_);
		stopping_ = false;
		pending_frame_.reset();
	}
	running_.store(true);
	try {
		worker_ = std::thread([this] { Run(); });
	} catch (...) {
		running_.store(false);
		return false;
	}
	return true;
}

void LatestVideoFrameQueue::Stop() noexcept {
	std::lock_guard<std::mutex> lifecycle_guard(lifecycle_mutex_);
	if (!running_.load() && !worker_.joinable()) {
		return;
	}
	running_.store(false);
	{
		std::lock_guard<std::mutex> guard(mutex_);
		stopping_ = true;
		pending_frame_.reset();
	}
	condition_.notify_all();
	if (worker_.joinable()) {
		worker_.join();
	}
	{
		std::lock_guard<std::mutex> guard(mutex_);
		stopping_ = false;
	}
}

bool LatestVideoFrameQueue::IsRunning() const noexcept { return running_.load(); }

bool LatestVideoFrameQueue::Push(const std::uint8_t* data, std::uint32_t width,
                                 std::uint32_t height, std::uint32_t row_stride_bytes,
                                 std::int64_t timestamp_us) noexcept {
	try {
		if (!running_.load() || data == nullptr || width == 0 || height == 0 ||
		    width > std::numeric_limits<std::uint32_t>::max() / 4U ||
		    row_stride_bytes < width * 4U ||
		    height > std::numeric_limits<std::size_t>::max() / row_stride_bytes) {
			return false;
		}
		OwnedBgraFrame frame;
		frame.data.resize(static_cast<std::size_t>(row_stride_bytes) * height);
		std::memcpy(frame.data.data(), data, frame.data.size());
		frame.width = width;
		frame.height = height;
		frame.row_stride_bytes = row_stride_bytes;
		frame.timestamp_us = timestamp_us;
		{
			std::lock_guard<std::mutex> guard(mutex_);
			if (!running_.load() || stopping_) {
				return false;
			}
			pending_frame_ = std::move(frame);
		}
		condition_.notify_one();
		return true;
	} catch (...) {
		return false;
	}
}

void LatestVideoFrameQueue::Run() noexcept {
	for (;;) {
		OwnedBgraFrame frame;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			condition_.wait(lock, [this] { return stopping_ || pending_frame_.has_value(); });
			if (stopping_) {
				return;
			}
			frame = std::move(*pending_frame_);
			pending_frame_.reset();
		}
		try {
			handler_(frame);
		} catch (...) {
			// Capture callbacks must not terminate the queue worker.
		}
	}
}

} // namespace livekit::capture
